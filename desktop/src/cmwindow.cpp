#include "cmwindow.h"

#include "appearancecontrol.h"
#include "cmconfig.h"
#include "cmlayout.h"
#include "cmmodel.h"
#include "cmsections.h"
#include "cmsettingsdialog.h"

#include "dockconfig.h"

#include "audiocontrol.h"
#include "batterycontrol.h"
#include "brightnesscontrol.h"
#include "desktopentry.h"
#include "docklink.h"
#include "iconprovider.h"
#include "mpriscontrol.h"
#include "networkcontrol.h"
#include "powercontrol.h"
#include "screenbrightness.h"
#include "theme.h"
#include "virtualdesktops.h"
#include "wallpapercontrol.h"
#include "weathercontrol.h"
#include "weatherlauncher.h"

#include <QColorDialog>
#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMargins>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSettings>
#include <QScreen>
#include <QTimer>

#include <QtWaylandClient/private/qwaylandscreen_p.h>

namespace {
// zwlr_layer_surface_v1 anchor bits
constexpr uint AnchorTop = 1;
constexpr uint AnchorBottom = 2;
constexpr uint AnchorLeft = 4;
constexpr uint AnchorRight = 8;
// Focus can bounce for a frame or two while the compositor maps the surface.
constexpr qint64 kFocusGraceMs = 400;

// Puts a Qt Widgets dialog *above* the panel.
//
// The panel is a layer surface on the top layer, and an ordinary xdg toplevel
// (which is what every QDialog is) is stacked below that layer by protocol — no
// amount of raise()/activateWindow() moves it, so the colour picker, the rename
// prompt and the settings dialog all opened behind a panel that covered them.
// The fix is to make the dialog a layer surface too, one layer higher
// (overlay), which the in-tree integration already supports: it only looks at
// these dynamic properties, and they have to be in place before the surface
// gets its shell role — which Qt Wayland gives it on the first show(), not when
// the QWindow appears, so a winId() here (the public way to force the handle)
// is early enough. Keyboard interactivity is what lets the user type a hex
// colour or a name; the panel keeps its own grab, and the compositor routes
// keys to the surface on top.
//
// A no-op off Wayland (the Xvfb harnesses), where dialogs behave normally.
void showAsOverlay(QWidget *w)
{
    if (!w || !QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return;
    w->winId();
    QWindow *handle = w->windowHandle();
    if (!handle)
        return;
    handle->setProperty("kdock.layershell", true);
    handle->setProperty("kdock.layer", 3u); // overlay: one above the panel
    handle->setProperty("kdock.keyboardInteractivity", 1u);
}
} // namespace

CmWindow::CmWindow(CmConfig *config, Theme *theme, CmLayout *layout, CmModel *model,
                   const CmBackends &backends)
    : m_config(config)
    , m_theme(theme)
    , m_layout(layout)
    , m_model(model)
    , m_backends(backends)
{
    // Same object the dock's own settings dialog uses for its theme pickers:
    // the panel's icon-set picker lists the same themes and honors the same
    // favorites, so one choice, both surfaces.
    m_appearance = new AppearanceControl(m_theme, this);

    setTitle(QStringLiteral("kdock-desktop"));
    setFlags(Qt::FramelessWindowHint);
    // The QML paints its own background at config.backgroundOpacity (0 by default
    // here), so the surface itself has to be able to show through: the wallpaper
    // and the windows below have to be visible everywhere a widget is not.
    setColor(Qt::transparent);
    // The compositor dictates the size (we anchor all four edges), and the QML
    // root fills it — the opposite of the dock, where the content decides.
    setResizeMode(QQuickView::SizeRootObjectToView);

    // Layer-shell properties must be in place before the platform window exists
    // (i.e. before show()).
    setProperty("kdock.layershell", true);
    // On-demand, never exclusive: a widget can take the keyboard when the user
    // clicks into it (a text field, say), but the canvas must never grab the
    // keyboard globally the way the pop-up panel does while it is open.
    setProperty("kdock.keyboardInteractivity", 2u);
    // NOT "dock" and NOT "desktop": a plain namespace so KWin types this surface
    // as Normal (scopeToType in layershellv1window.cpp, see layershell.h). That
    // keeps it below ordinary windows (they can cover it) yet above the wallpaper
    // — and, unlike "desktop", it will not be mistaken for the Overview
    // background nor collide with kdock's own wallpaper surface.
    setProperty("kdock.scope", QStringLiteral("kdock-desktop"));
    applyLayerProperties();

    connect(m_config, &CmConfig::windowChanged, this, [this] {
        applyLayerProperties();
        applySize();
        emit windowFrameChanged();
    });
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &CmWindow::scheduleApplyScreen);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &CmWindow::scheduleApplyScreen);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &CmWindow::scheduleApplyScreen);
    connect(m_config, &CmConfig::sectionsChanged, this, &CmWindow::sectionsChanged);
    // A background change can flip which icon set reads on it, and a theme edit
    // bumps the revision that busts QML's pixmap cache.
    connect(m_config, &CmConfig::settingsChanged, this, &CmWindow::iconSuffixChanged);
    if (m_theme)
        connect(m_theme, &Theme::changed, this, &CmWindow::iconSuffixChanged);

    engine()->addImageProvider(QStringLiteral("icon"), new IconProvider);

    rootContext()->setContextProperty(QStringLiteral("cmConfig"), m_config);
    rootContext()->setContextProperty(QStringLiteral("theme"), m_theme);
    rootContext()->setContextProperty(QStringLiteral("cmLayout"), m_layout);
    rootContext()->setContextProperty(QStringLiteral("cards"), m_model);
    rootContext()->setContextProperty(QStringLiteral("win"), this);
    // One context property per backend. A null one is `undefined` in QML, which
    // is exactly the test each card already has to make ("is this available?").
    rootContext()->setContextProperty(QStringLiteral("apps"), m_backends.apps);
    rootContext()->setContextProperty(QStringLiteral("audio"), m_backends.audio);
    rootContext()->setContextProperty(QStringLiteral("battery"), m_backends.battery);
    rootContext()->setContextProperty(QStringLiteral("brightness"), m_backends.brightness);
    rootContext()->setContextProperty(QStringLiteral("screens"), m_backends.screenBrightness);
    rootContext()->setContextProperty(QStringLiteral("network"), m_backends.network);
    rootContext()->setContextProperty(QStringLiteral("weather"), m_backends.weather);
    rootContext()->setContextProperty(QStringLiteral("weatherLauncher"),
                                      m_backends.weatherLauncher);
    rootContext()->setContextProperty(QStringLiteral("wallpaperControl"), m_backends.wallpaper);
    rootContext()->setContextProperty(QStringLiteral("power"), m_backends.power);
    rootContext()->setContextProperty(QStringLiteral("mpris"), m_backends.mpris);
    rootContext()->setContextProperty(QStringLiteral("dock"), m_backends.dock);
    // Same names the dock's own QML uses for these two, so the popups and the
    // cards read alike.
    rootContext()->setContextProperty(QStringLiteral("appearance"), m_backends.appearance);
    rootContext()->setContextProperty(QStringLiteral("virtualDesktops"), m_backends.desktops);

    applyScreen();
    applySize();
    setSource(QUrl(QStringLiteral("qrc:/qml/ControlManager.qml")));

    connect(this, &QWindow::activeChanged, this, &CmWindow::onActiveChanged);
}

QVariantList CmWindow::sections() const
{
    return CmSections::toVariantList();
}

QString CmWindow::iconSuffix() const
{
    QString suffix = QStringLiteral("@") + QString::number(m_theme ? m_theme->revision() : 0);

    // An explicit panel icon set wins over everything: the user asked for these
    // icons, not for the luminance-adapted pair.
    const QString overrideTheme = m_config->iconTheme();
    if (!overrideTheme.isEmpty()) {
        suffix += QLatin1Char('@') + overrideTheme;
        return suffix;
    }

    // Which of the two sets to ask for: the panel's own background decides, not
    // the desktop's. Same perceptual test the dock uses for its clock colour.
    QColor base = m_config->backgroundMode() == 1 && m_config->backgroundColorSet()
                      ? m_config->backgroundColor()
                      : (m_theme ? m_theme->background() : QColor(Qt::black));
    const qreal luma = 0.299 * base.redF() + 0.587 * base.greenF() + 0.114 * base.blueF();

    // The dock's own two settings, read from the shared file: one choice, both
    // surfaces.
    QSettings shared(DockConfig::settingsFilePath(), QSettings::IniFormat);
    const QString set = luma < 0.5
        ? shared.value(QStringLiteral("widgetIconThemeDarkBg"),
                       QStringLiteral("breeze-dark")).toString()
        : shared.value(QStringLiteral("widgetIconThemeLightBg"),
                       QStringLiteral("breeze")).toString();
    if (!set.isEmpty())
        suffix += QLatin1Char('@') + set;
    return suffix;
}

void CmWindow::setCurrentTab(const QString &tab)
{
    if (m_currentTab == tab)
        return;
    m_currentTab = tab;
    if (m_config->rememberTab())
        m_config->setLastTab(tab);
    emit currentTabChanged();
}

void CmWindow::scheduleApplyScreen()
{
    if (m_screenChangePending)
        return;
    m_screenChangePending = true;
    QTimer::singleShot(0, this, [this] {
        m_screenChangePending = false;
        applyScreen();
        applySize();
    });
}

void CmWindow::applyScreen()
{
    QScreen *target = nullptr;
    if (!m_screenName.isEmpty()) {
        const auto screens = QGuiApplication::screens();
        for (QScreen *s : screens) {
            if (s->name() == m_screenName) {
                target = s;
                break;
            }
        }
    }
    if (!target)
        target = QGuiApplication::primaryScreen();
    if (!target)
        return;

    // Resolve the target's wl_output here and stash it on the window, so the
    // layer-shell integration binds the surface to exactly this output instead
    // of guessing from QWindow::screen() (which lags behind setScreen() right
    // after a destroy()+show() cycle).
    quintptr outputPtr = 0;
    if (auto *qws = dynamic_cast<QtWaylandClient::QWaylandScreen *>(target->handle()))
        outputPtr = reinterpret_cast<quintptr>(qws->output());
    setProperty("kdock.output", QVariant::fromValue(outputPtr));

    const bool needsRecreate = !handle()
        || (outputPtr ? outputPtr != m_boundOutput : screen() != target);
    m_boundOutput = outputPtr;
    if (!needsRecreate)
        return;

    const bool wasVisible = isVisible();
    if (handle()) {
        hide();
        destroy();
    }
    setScreen(target);
    if (wasVisible) {
        create();
        show();
    }
}

void CmWindow::applySize()
{
    // Always the whole screen. The layer surface is anchored to all four edges,
    // so the compositor is the real authority on the size (see sendSize, which
    // sends 0 for both stretched axes); this resize just gives the QML root a
    // sensible size before the first configure arrives.
    QScreen *s = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect geo = s ? s->geometry() : QRect(0, 0, 1920, 1080);
    resize(geo.width(), geo.height());
}

uint CmWindow::layerAnchors(QMargins *margins) const
{
    uint anchor = AnchorTop;
    QMargins m4;
    const int m = m_config->screenMargin();
    const bool horizontal = m_config->edge() == CmConfig::Top
                            || m_config->edge() == CmConfig::Bottom;
    switch (m_config->edge()) {
    case CmConfig::Top:    anchor = AnchorTop;    m4.setTop(m);    break;
    case CmConfig::Bottom: anchor = AnchorBottom; m4.setBottom(m); break;
    case CmConfig::Left:   anchor = AnchorLeft;   m4.setLeft(m);   break;
    case CmConfig::Right:  anchor = AnchorRight;  m4.setRight(m);  break;
    }

    // Alignment along the anchored edge. A single-edge anchor already centers
    // the surface, so Center adds nothing.
    if (m_config->alignment() == CmConfig::Start) {
        anchor |= horizontal ? AnchorLeft : AnchorTop;
        if (horizontal) m4.setLeft(m); else m4.setTop(m);
    } else if (m_config->alignment() == CmConfig::End) {
        anchor |= horizontal ? AnchorRight : AnchorBottom;
        if (horizontal) m4.setRight(m); else m4.setBottom(m);
    }

    if (margins)
        *margins = m4;
    return anchor;
}

qreal CmWindow::originShift(bool horizontal) const
{
    const uint a = layerAnchors();
    const uint near = horizontal ? AnchorLeft : AnchorTop;
    const uint far = horizontal ? AnchorRight : AnchorBottom;
    const bool n = a & near;
    const bool f = a & far;
    if (n && !f)
        return 0.0;   // left/top edge pinned: the surface only grows away from it
    if (f && !n)
        return -1.0;  // far edge pinned: the origin backs off pixel per pixel
    return -0.5;      // free (or stretched) on this axis: it grows both ways
}

void CmWindow::applyLayerProperties()
{
    // Edge to edge, always: anchor all four edges and let the compositor stretch
    // the surface across the whole output. The configured edge/alignment/margin
    // (still there for the settings dialog, and unused here) play no part — the
    // canvas is the screen.
    setProperty("kdock.anchors", AnchorTop | AnchorBottom | AnchorLeft | AnchorRight);
    setProperty("kdock.margins", QVariant::fromValue(QMargins()));
    // Bottom, not top: below the dock (which is on the top layer) and below every
    // ordinary window, so windows cover it — but above the wallpaper (layer 0).
    setProperty("kdock.layer", 1u); // bottom
    // -1, not 0: "do not shrink me to make room for anybody's exclusive zone".
    // With 0 the compositor would carve the dock's strut (and any other panel's)
    // out of the canvas; -1 makes it span the full output whether the docks are
    // shown or hidden.
    setProperty("kdock.exclusiveZone", -1);
}

void CmWindow::showOn(const QString &screenName)
{
    if (!screenName.isEmpty() && screenName != m_screenName) {
        m_screenName = screenName;
        applyScreen();
    }
    applySize();

    // "Remember the tab" off means every open starts at Principal.
    if (!m_config->rememberTab())
        setCurrentTab(QString());
    else if (m_currentTab.isEmpty() && !m_config->lastTab().isEmpty())
        setCurrentTab(m_config->lastTab());

    m_shownAt = QDateTime::currentMSecsSinceEpoch();
    // On-demand keyboard, and no requestActivate(): this is a canvas that lives
    // under the windows, not a panel. Grabbing the keyboard or activating the
    // surface would steal focus from whatever the user is actually working in.
    setProperty("kdock.keyboardInteractivity", 2u);
    show();
}

void CmWindow::hidePanel()
{
    // Give the keyboard back before going away, or the compositor keeps routing
    // keys at a surface nobody can see.
    setProperty("kdock.keyboardInteractivity", 0u);
    // hide(), never destroy(): the whole point of staying resident is that the
    // next click is instant.
    hide();
}

bool CmWindow::blockingClose() const
{
    if (m_modalDepth > 0)
        return true;
    return m_settingsDialog && m_settingsDialog->isVisible();
}

void CmWindow::onActiveChanged()
{
    if (isActive() || !isVisible())
        return;
    if (m_config->keepOpen() || !m_config->closeOnFocusLoss())
        return;
    if (blockingClose())
        return;
    if (QDateTime::currentMSecsSinceEpoch() - m_shownAt < kFocusGraceMs)
        return;
    hidePanel();
}

void CmWindow::setPanelSize(int w, int h)
{
    QScreen *s = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect geo = s ? s->geometry() : QRect(0, 0, 1920, 1080);
    w = qBound(320, w, geo.width());
    h = qBound(240, h, geo.height());
    // Dragging writes absolute pixels, so a configured percentage stops
    // applying (the window group's px spinboxes re-enable through
    // windowChanged, and its percent spinboxes re-read it).
    if (m_config->panelWidthPercent() != 0)
        m_config->setPanelWidthPercent(0);
    if (m_config->panelHeightPercent() != 0)
        m_config->setPanelHeightPercent(0);
    m_config->setPanelWidth(w);
    m_config->setPanelHeight(h);
}

void CmWindow::openSettings()
{    if (!m_settingsDialog) {
        m_settingsDialog = new CmSettingsDialog(m_config, m_layout, m_appearance);
        // Same reason as the modal dialogs: an ordinary toplevel opens *under*
        // the panel's layer surface, which is how this one used to end up
        // unreachable behind it.
        showAsOverlay(m_settingsDialog);
    }
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void CmWindow::reloadConfig()
{
    m_config->reloadFromDisk();
}

void CmWindow::quitApp()
{
    QCoreApplication::quit();
}

void CmWindow::restartSelf()
{
    QStringList args = QCoreApplication::arguments();
    if (!args.isEmpty())
        args.removeFirst();
    QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
    QCoreApplication::quit();
}

bool CmWindow::launchDesktop(const QString &id)
{
    if (!m_backends.apps)
        return false;
    const DesktopEntry entry = m_backends.apps->byId(id);
    if (entry.id.isEmpty())
        return false;
    m_backends.apps->launch(entry);
    if (!m_config->keepOpen())
        hidePanel();
    return true;
}

bool CmWindow::runCommand(const QString &program, const QStringList &args)
{
    if (program.isEmpty())
        return false;
    return QProcess::startDetached(program, args);
}

bool CmWindow::runScript(const QString &path, const QString &screenName)
{
    if (path.isEmpty())
        return false;
    auto *proc = new QProcess(this);
    if (!screenName.isEmpty()) {
        // The variable next-wall.sh reads to limit itself to one monitor; the
        // dock exports the same one for its Script Runners.
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("KDOCK_SCREEN"), screenName);
        proc->setProcessEnvironment(env);
    }
    connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
    // bash, not sh: the wallpaper scripts use arrays and process substitution,
    // which dash does not have (learned the hard way, see the memory of
    // next-wall.sh).
    proc->start(QStringLiteral("bash"), {path});
    return proc->waitForStarted(2000);
}

void CmWindow::retranslate()
{
    engine()->retranslate();
    emit sectionsChanged();
    if (!m_settingsDialog)
        return;
    const bool wasVisible = m_settingsDialog->isVisible();
    m_settingsDialog->deleteLater();
    m_settingsDialog = nullptr;
    if (wasVisible)
        openSettings();
}

// The three dialogs below build their widget instead of calling the static
// convenience function: showAsOverlay() needs the QWidget to exist before it is
// shown, and the statics show it themselves.

QString CmWindow::pickColor(const QString &current)
{
    ++m_modalDepth;
    QColorDialog dialog;
    dialog.setWindowTitle(tr("Color de la tarjeta"));
    if (QColor(current).isValid())
        dialog.setCurrentColor(QColor(current));
    showAsOverlay(&dialog);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    const QColor chosen = dialog.selectedColor();
    --m_modalDepth;
    requestActivate();
    return accepted && chosen.isValid() ? chosen.name() : QString();
}

QString CmWindow::promptText(const QString &title, const QString &label, const QString &current)
{
    ++m_modalDepth;
    QInputDialog dialog;
    dialog.setWindowTitle(title);
    dialog.setLabelText(label);
    dialog.setTextValue(current);
    dialog.setTextEchoMode(QLineEdit::Normal);
    dialog.setInputMode(QInputDialog::TextInput);
    showAsOverlay(&dialog);
    const bool ok = dialog.exec() == QDialog::Accepted;
    const QString text = dialog.textValue();
    --m_modalDepth;
    requestActivate();
    return ok ? text : QString();
}

bool CmWindow::confirm(const QString &title, const QString &text)
{
    ++m_modalDepth;
    QMessageBox box(QMessageBox::Question, title, text,
                    QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    showAsOverlay(&box);
    const auto answer = static_cast<QMessageBox::StandardButton>(box.exec());
    --m_modalDepth;
    requestActivate();
    return answer == QMessageBox::Yes;
}
