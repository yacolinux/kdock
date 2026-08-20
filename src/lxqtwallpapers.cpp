#include "lxqtwallpapers.h"

#include "desktopentry.h"
#include "desktopwallpapers.h"
#include "dockconfig.h"
#include "virtualdesktops.h"
#include "wallpaperfolder.h"
#include "wallpaperwindow.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDebug>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>

namespace {

const QString kPcmanfmService = QStringLiteral("org.pcmanfm.PCManFM");
const QString kPcmanfmPath = QStringLiteral("/Application");
const QString kPcmanfmIface = QStringLiteral("org.pcmanfm.Application");

QString slideshowStateKey(int desktop, const QString &screen)
{
    return QStringLiteral("%1/%2").arg(desktop).arg(screen);
}

} // namespace

LxqtWallpapers::LxqtWallpapers(VirtualDesktops *desktops, QObject *parent)
    : QObject(parent)
    , m_desktops(desktops)
{
    m_slideshow.setSingleShot(false);
    connect(&m_slideshow, &QTimer::timeout, this, [this] { advance(); });

    m_advanceDebounce.setSingleShot(true);
    m_advanceDebounce.setInterval(kAdvanceDelayMs);
    connect(&m_advanceDebounce, &QTimer::timeout, this, [this] {
        apply(m_currentDesktop, /*advance=*/true);
    });

    if (m_desktops) {
        connect(m_desktops, &VirtualDesktops::currentChanged, this,
                &LxqtWallpapers::onDesktopChanged);
    }
    connect(qGuiApp, &QGuiApplication::screenAdded, this,
            [this](QScreen *) { onScreensChanged(); });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this,
            [this](QScreen *) { onScreensChanged(); });
}

LxqtWallpapers::~LxqtWallpapers()
{
    // Not quit(): a destructor running at the end of main() is too late to talk
    // to the session bus, and quit() has already run by then on both paths.
    qDeleteAll(m_windows);
    m_windows.clear();
}

bool LxqtWallpapers::enabled()
{
    return DesktopWallpapers::enabled();
}

void LxqtWallpapers::start()
{
    if (!enabled()) {
        // The feature is off — but a previous run may have died holding the
        // desktop (SIGKILL, a crash, a power cut), and nothing else will ever
        // give it back. This is the only moment kdock can tell: the flag is
        // set and we are not the ones drawing.
        if (desktopSuppressed()) {
            qInfo("kdock: restoring PCManFM's desktop left suppressed by a previous run");
            setPcmanfmDesktop(true);
        }
        return;
    }

    m_currentDesktop = m_desktops ? m_desktops->currentPosition() : 1;
    if (m_currentDesktop <= 0)
        m_currentDesktop = 1; // no KWin (or no answer yet): treat as desktop 1

    m_active = true;
    emit activeChanged();
    // PCManFM is not asked to stand down here: apply() does it, and only once
    // it has a picture of its own on screen. The master switch alone is not
    // enough — it is shared with the Plasma engine, so a config carried over
    // from Plasma arrives with it already on and (as that engine kept desktop 1
    // for KDE) possibly nothing configured at all. Suppressing on the strength
    // of the flag would black out the desktop and take the icons with it.
    apply(m_currentDesktop);
}

void LxqtWallpapers::stop()
{
    if (!m_active)
        return;
    m_active = false;
    emit activeChanged();
    m_slideshow.stop();
    m_advanceDebounce.stop();
    qDeleteAll(m_windows);
    m_windows.clear();
    if (m_suppressed) {
        m_suppressed = false;
        setPcmanfmDesktop(true);
    }
}

void LxqtWallpapers::quit()
{
    stop();
}

void LxqtWallpapers::onDesktopChanged(int position)
{
    if (!m_active || position <= 0)
        return;
    // Past the configurable range the user never said what to show, so the
    // surfaces stay on the last thing they had rather than going black.
    if (position > DockConfig::kMaxDesktops)
        return;
    m_currentDesktop = position;
    // Two steps, and the split is the point.
    //
    // Show this desktop's own picture **now**, without advancing: leaving the
    // previous desktop's wallpaper up for the debounce would be a visible lag
    // on an ordinary switch, which is the common case.
    apply(position, /*advance=*/false);
    // And step it only once the user has actually settled here — see
    // kAdvanceDelayMs. Restarting the timer is what makes a burst of switches
    // (navigating the Overview) advance the destination and nothing else.
    m_advanceDebounce.start();
}

void LxqtWallpapers::onScreensChanged()
{
    if (!m_active)
        return;
    apply(m_currentDesktop);
}

void LxqtWallpapers::syncWindows()
{
    const auto screens = QGuiApplication::screens();
    QStringList live;
    for (QScreen *s : screens) {
        if (s->name().isEmpty())
            continue;
        live << s->name();
        if (!m_windows.contains(s->name()))
            m_windows.insert(s->name(), new WallpaperWindow(s->name()));
    }
    // A monitor that went away takes its surface with it.
    const auto known = m_windows.keys();
    for (const QString &name : known) {
        if (!live.contains(name))
            delete m_windows.take(name);
    }
}

QString LxqtWallpapers::pcmanfmWallpaper()
{
    // The profile is the one LXQt's autostart entry passes (--profile=lxqt);
    // read it from there rather than assuming, since a session can use another.
    QString profile = QStringLiteral("lxqt");
    const QStringList entries = QStandardPaths::locateAll(
        QStandardPaths::GenericConfigLocation,
        QStringLiteral("autostart/lxqt-desktop.desktop"));
    for (const QString &file : entries) {
        const DesktopEntry entry = DesktopEntryIndex::fromFile(file);
        for (const QString &arg : QProcess::splitCommand(entry.exec)) {
            if (arg.startsWith(QLatin1String("--profile=")))
                profile = arg.mid(10);
        }
        if (entry.isValid())
            break;
    }

    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                         + QStringLiteral("/pcmanfm-qt/") + profile
                         + QStringLiteral("/settings.conf");
    if (!QFileInfo::exists(path))
        return {};
    return QSettings(path, QSettings::IniFormat).value(QStringLiteral("Desktop/Wallpaper"))
        .toString();
}

QString LxqtWallpapers::advanceFor(int desktop, const QString &screen)
{
    if (!DesktopWallpapers::slideshowEnabled(desktop))
        return {};
    const QString folder = DesktopWallpapers::slideshowFolder(desktop, screen);
    if (folder.isEmpty())
        return {};

    const QString key = slideshowStateKey(desktop, screen);
    QStringList history = m_slideshowHistory.value(key);
    const QString picked = WallpaperFolder::randomOther({folder}, history);
    if (picked.isEmpty())
        return {};

    history.append(picked);
    while (history.size() > kHistory)
        history.removeFirst();
    m_slideshowHistory.insert(key, history);
    return picked;
}

QString LxqtWallpapers::imageFor(int desktop, const QString &screen)
{
    if (!DesktopWallpapers::slideshowEnabled(desktop)) {
        const QString image = DesktopWallpapers::imageFor(desktop, screen);
        return image.isEmpty() ? pcmanfmWallpaper() : image;
    }

    const QString folder = DesktopWallpapers::slideshowFolder(desktop, screen);
    if (folder.isEmpty())
        return pcmanfmWallpaper();

    const QStringList history = m_slideshowHistory.value(slideshowStateKey(desktop, screen));
    const QString current = history.isEmpty() ? QString() : history.constLast();
    // Nothing picked yet, or the picture is gone from the folder: pick one now.
    if (current.isEmpty() || !QFileInfo::exists(current))
        return advanceFor(desktop, screen);
    return current;
}

void LxqtWallpapers::apply(int desktop, bool advance)
{
    if (!m_active)
        return;
    syncWindows();

    const int fill = DesktopWallpapers::fillMode();
    bool drewSomething = false;
    for (auto it = m_windows.begin(); it != m_windows.end(); ++it) {
        QString image;
        if (advance)
            image = advanceFor(desktop, it.key());
        // Not in slideshow mode, or no folder: advanceFor() said nothing, so
        // fall back to whatever this pair should be showing anyway. A static
        // wallpaper has no "next", and asking for one must not blank it.
        if (image.isEmpty())
            image = imageFor(desktop, it.key());
        drewSomething = drewSomething || !image.isEmpty();
        it.value()->setImage(image, fill);
    }

    // The one place PCManFM is asked to drop the desktop, and only once there
    // is something of ours covering it. Never undone here: a desktop with
    // nothing configured would otherwise bring PCManFM back on every switch,
    // and since it exits when its last window goes (no --daemon-mode) that
    // would mean relaunching it each time.
    if (drewSomething && !m_suppressed) {
        m_suppressed = true;
        setPcmanfmDesktop(false);
    }

    updateTimer(desktop);
    emit wallpapersApplied(desktop);
}

void LxqtWallpapers::advance(const QString &screenName)
{
    if (!m_active)
        return;
    // **Only the current virtual desktop.** The others keep their pictures
    // untouched: they are not on screen, so moving them would be work nobody
    // can see, and it would burn their history for no reason.
    const int desktop = m_currentDesktop;
    if (!DesktopWallpapers::slideshowEnabled(desktop))
        return; // a static wallpaper has no next

    bool moved = false;
    for (auto it = m_windows.begin(); it != m_windows.end(); ++it) {
        if (!screenName.isEmpty() && it.key() != screenName)
            continue;
        const QString picked = advanceFor(desktop, it.key());
        if (picked.isEmpty())
            continue;
        it.value()->setImage(picked, DesktopWallpapers::fillMode());
        moved = true;
    }
    if (moved)
        emit wallpapersApplied(desktop);
}

void LxqtWallpapers::updateTimer(int desktop)
{
    if (!DesktopWallpapers::slideshowEnabled(desktop)) {
        m_slideshow.stop();
        return;
    }
    // Seconds in the config, milliseconds in the timer — KDE's slideshow takes
    // seconds, so the stored key kept seconds too (see DesktopWallpapers).
    const int seconds = qMax(5, DesktopWallpapers::slideshowInterval(desktop));
    m_slideshow.start(seconds * 1000);
}

bool LxqtWallpapers::desktopSuppressed()
{
    return QSettings(DockConfig::settingsFilePath(), QSettings::IniFormat)
        .value(QStringLiteral("Wallpapers/lxqtDesktopSuppressed"), false)
        .toBool();
}

void LxqtWallpapers::setDesktopSuppressed(bool on)
{
    QSettings s(DockConfig::settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("Wallpapers/lxqtDesktopSuppressed"), on);
    // Written through now, not whenever QSettings feels like it: the whole
    // point of this key is to survive a process that does not get to finish.
    s.sync();
}

void LxqtWallpapers::setPcmanfmDesktop(bool on)
{
    setDesktopSuppressed(!on);

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusConnectionInterface *iface = bus.interface();
    const bool running = iface && iface->isServiceRegistered(kPcmanfmService);

    if (running) {
        QDBusMessage msg = QDBusMessage::createMethodCall(kPcmanfmService, kPcmanfmPath,
                                                          kPcmanfmIface,
                                                          QStringLiteral("desktopManager"));
        msg.setArguments({on});
        bus.asyncCall(msg);
        return;
    }

    if (!on)
        return; // nobody to switch off

    // PCManFM is gone — which is the normal outcome of having switched its
    // desktop off, because LXQt starts it without --daemon-mode and deleting
    // its last window makes it quit. Bring it back the way the session does,
    // reading the command out of LXQt's own autostart entry instead of guessing
    // the profile name.
    const QStringList files = QStandardPaths::locateAll(
        QStandardPaths::GenericConfigLocation,
        QStringLiteral("autostart/lxqt-desktop.desktop"));
    for (const QString &file : files) {
        const DesktopEntry entry = DesktopEntryIndex::fromFile(file);
        if (entry.isValid()) {
            DesktopEntryIndex::launch(entry);
            return;
        }
    }
    QProcess::startDetached(QStringLiteral("pcmanfm-qt"),
                            {QStringLiteral("--desktop"), QStringLiteral("--profile=lxqt")});
}
