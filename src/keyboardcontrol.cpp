#include "keyboardcontrol.h"

#include "dockconfig.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QFile>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

#include <utility>

// The argument type of org.kde.kconfig.notify.ConfigChanged: group name ->
// changed keys, i.e. a{saay} on the wire. Same shape KConfigWatcher expects on
// the receiving side (QHash<QString, QByteArrayList>).
using KConfigNotifyMap = QHash<QString, QByteArrayList>;
Q_DECLARE_METATYPE(KConfigNotifyMap)

namespace {

const char *kEnabledKey = "Keyboard/enabled";
const char *kLayoutKey = "Keyboard/layout";
const char *kVariantKey = "Keyboard/variant";
const char *kModelKey = "Keyboard/model";
const char *kOptionsKey = "Keyboard/options";

QSettings shared()
{
    return QSettings(DockConfig::settingsFilePath(), QSettings::IniFormat);
}

// The kxkbrc keys, in QSettings form. `[Layout]` is a section like any other,
// so it *is* addressed with the group prefix — the top-level mapping that bites
// kdeglobals' ColorScheme only applies to `[General]`.
const char *kConfLayout = "Layout/LayoutList";
const char *kConfVariant = "Layout/VariantList";
const char *kConfModel = "Layout/Model";
const char *kConfOptions = "Layout/Options";

QString readKxkbrc(const char *key)
{
    const QString path = KeyboardControl::kxkbrcPath();
    if (path.isEmpty() || !QFile::exists(path))
        return {};
    QSettings kxkb(path, QSettings::IniFormat);
    // Same guard buildPalette() documents in QtCompat: QSettings caches one copy
    // per path and revalidates it by (mtime, size), and a layout id swapped for
    // another of the same length ("es" for "us") inside one millisecond would be
    // served stale. Costs one stat().
    kxkb.sync();
    return kxkb.value(QLatin1String(key)).toString();
}

QString findKwriteconfig()
{
    static const QStringList dirs = {QStringLiteral("/opt/kde/bin"), QStringLiteral("/usr/bin")};
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("kwriteconfig6"));
    if (!onPath.isEmpty())
        return onPath;
    return QStandardPaths::findExecutable(QStringLiteral("kwriteconfig6"), dirs);
}

// One section of an xkb rules list, e.g. everything under "! layout" up to the
// next "!" line. Each row is `<id><whitespace><description>`.
QList<KeyboardControl::Entry> parseRules(const QString &section,
                                         const QString &variantsOfLayout = QString())
{
    QList<KeyboardControl::Entry> out;
    const QString path = KeyboardControl::rulesPath();
    if (path.isEmpty())
        return out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;

    QTextStream in(&f);
    bool inSection = false;
    while (!in.atEnd()) {
        const QString raw = in.readLine();
        const QString line = raw.trimmed();
        if (raw.startsWith(QLatin1Char('!'))) {
            const QString head = line.mid(1).trimmed();
            // A section header is one bare word ("! layout"). The same syntax
            // also carries variable assignments ("! $nonlatin = am ara be"),
            // and treating one of those as a header would silently *end* the
            // section it appears in and drop every row after it. Anything that
            // is not a bare word leaves the state alone.
            if (head.contains(QLatin1Char(' ')) || head.contains(QLatin1Char('\t'))
                || head.contains(QLatin1Char('=')))
                continue;
            inSection = (head == section);
            continue;
        }
        if (!inSection || line.isEmpty())
            continue;
        const int split = line.indexOf(QRegularExpression(QStringLiteral("\\s")));
        if (split <= 0)
            continue;
        KeyboardControl::Entry e;
        e.id = line.left(split);
        e.name = line.mid(split).trimmed();
        if (!variantsOfLayout.isEmpty()) {
            // A variant row's description is "<layout>: <description>", and the
            // layout part is the only thing that says which layout it belongs
            // to — the id alone is ambiguous ("nodeadkeys" exists for a dozen).
            const int colon = e.name.indexOf(QLatin1Char(':'));
            if (colon < 0)
                continue;
            if (e.name.left(colon).trimmed() != variantsOfLayout)
                continue;
            e.name = e.name.mid(colon + 1).trimmed();
        }
        out.append(e);
    }
    return out;
}

// One shell-style assignment of /etc/default/keyboard (XKBLAYOUT="latam").
QString readDefaultKeyboard(const QString &key)
{
    QFile f(QStringLiteral("/etc/default/keyboard"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream in(&f);
    const QString prefix = key + QLatin1Char('=');
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (!line.startsWith(prefix))
            continue;
        QString value = line.mid(prefix.size()).trimmed();
        if (value.size() >= 2 && (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\'')))
            && value.endsWith(value.at(0))) {
            value = value.mid(1, value.size() - 2);
        }
        return value;
    }
    return {};
}

} // namespace

KeyboardControl::KeyboardControl(QObject *parent)
    : QObject(parent)
{
    // Nothing is applied at construction: main() decides when (and whether) the
    // session's keymap gets touched. See the header on why this class must be
    // inert until asked.
}

bool KeyboardControl::enabled()
{
    return shared().value(QLatin1String(kEnabledKey), false).toBool();
}

void KeyboardControl::setEnabled(bool on)
{
    if (on == enabled())
        return;
    QSettings s = shared();
    s.setValue(QLatin1String(kEnabledKey), on);
    s.sync();
    if (on)
        apply(true);
    emit changed();
}

QString KeyboardControl::layout()
{
    return shared().value(QLatin1String(kLayoutKey)).toString();
}

QString KeyboardControl::variant()
{
    return shared().value(QLatin1String(kVariantKey)).toString();
}

QString KeyboardControl::model()
{
    return shared().value(QLatin1String(kModelKey)).toString();
}

QString KeyboardControl::options()
{
    return shared().value(QLatin1String(kOptionsKey)).toString();
}

void KeyboardControl::setLayout(const QString &id)
{
    QSettings s = shared();
    s.setValue(QLatin1String(kLayoutKey), id);
    s.sync();
    apply();
    emit changed();
}

void KeyboardControl::setVariant(const QString &id)
{
    QSettings s = shared();
    s.setValue(QLatin1String(kVariantKey), id);
    s.sync();
    apply();
    emit changed();
}

void KeyboardControl::setModel(const QString &id)
{
    QSettings s = shared();
    s.setValue(QLatin1String(kModelKey), id);
    s.sync();
    apply();
    emit changed();
}

void KeyboardControl::setOptions(const QString &value)
{
    QSettings s = shared();
    s.setValue(QLatin1String(kOptionsKey), value);
    s.sync();
    apply();
    emit changed();
}

QString KeyboardControl::kxkbrcPath()
{
    // The same location kwriteconfig6 --file kxkbrc writes to: the first
    // writable generic config dir, i.e. $XDG_CONFIG_HOME. Built rather than
    // located so it is also correct before the file exists.
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (dir.isEmpty())
        return {};
    return dir + QStringLiteral("/kxkbrc");
}

QString KeyboardControl::configuredLayout()
{
    return readKxkbrc(kConfLayout);
}

QString KeyboardControl::configuredVariant()
{
    return readKxkbrc(kConfVariant);
}

QString KeyboardControl::configuredModel()
{
    return readKxkbrc(kConfModel);
}

QString KeyboardControl::configuredOptions()
{
    return readKxkbrc(kConfOptions);
}

QString KeyboardControl::rulesPath()
{
    // The seam is read-only, so it is safe to honour unconditionally: it only
    // decides which catalogue the pickers list.
    const QString fromEnv = qEnvironmentVariable("KDOCK_TEST_XKB_RULES");
    if (!fromEnv.isEmpty())
        return fromEnv;

    QStringList candidates;
    const QString root = qEnvironmentVariable("XKB_CONFIG_ROOT");
    if (!root.isEmpty())
        candidates << root + QStringLiteral("/rules/evdev.lst");
    candidates << QStringLiteral("/usr/share/X11/xkb/rules/evdev.lst")
               << QStringLiteral("/usr/local/share/X11/xkb/rules/evdev.lst")
               // evdev is the modern ruleset; base.lst is the same format and
               // is what older or minimal xkb-data ships.
               << QStringLiteral("/usr/share/X11/xkb/rules/base.lst");
    for (const QString &c : std::as_const(candidates)) {
        if (QFile::exists(c))
            return c;
    }
    return {};
}

QList<KeyboardControl::Entry> KeyboardControl::availableLayouts()
{
    return parseRules(QStringLiteral("layout"));
}

QList<KeyboardControl::Entry> KeyboardControl::availableVariants(const QString &layout)
{
    if (layout.isEmpty())
        return {};
    return parseRules(QStringLiteral("variant"), layout);
}

QList<KeyboardControl::Entry> KeyboardControl::availableModels()
{
    return parseRules(QStringLiteral("model"));
}

QString KeyboardControl::systemLayout()
{
    return readDefaultKeyboard(QStringLiteral("XKBLAYOUT"));
}

QString KeyboardControl::systemVariant()
{
    return readDefaultKeyboard(QStringLiteral("XKBVARIANT"));
}

QString KeyboardControl::systemModel()
{
    return readDefaultKeyboard(QStringLiteral("XKBMODEL"));
}

QList<KeyboardControl::Pending> KeyboardControl::pendingWrites() const
{
    QList<Pending> out;
    const QString wantLayout = layout();
    if (wantLayout.isEmpty())
        return out; // nothing configured: there is no batch to write

    out.append({QString::fromLatin1(kConfLayout), wantLayout, false});
    // Written even when empty: an empty VariantList is how a stale variant gets
    // cleared, and leaving the key alone would keep applying it.
    out.append({QString::fromLatin1(kConfVariant), variant(), false});

    const QString wantModel = model();
    out.append({QString::fromLatin1(kConfModel), wantModel, wantModel.isEmpty()});
    const QString wantOptions = options();
    out.append({QString::fromLatin1(kConfOptions), wantOptions, wantOptions.isEmpty()});
    return out;
}

void KeyboardControl::notifyKWin(const QStringList &keys)
{
    if (keys.isEmpty())
        return;
    static bool registered = false;
    if (!registered) {
        // Both, and the inner one first. Registering only the map marshals
        // nothing: QtDBus reports `type 'QByteArrayList' is not registered`,
        // then `QHash<QString,QList<QByteArray>> produces invalid D-Bus
        // signature '<empty>'`, and the signal goes out malformed — which on
        // the receiving end is indistinguishable from never having been sent.
        // Caught by running the probe against a real bus (dbus-run-session);
        // the file was written correctly the whole time.
        qDBusRegisterMetaType<QByteArrayList>();
        qDBusRegisterMetaType<KConfigNotifyMap>();
        registered = true;
    }

    QByteArrayList changed;
    changed.reserve(keys.size());
    for (const QString &k : keys)
        changed.append(k.toUtf8());

    KConfigNotifyMap payload;
    payload.insert(QStringLiteral("Layout"), changed);

    // Path is "/" + the config file's name, interface org.kde.kconfig.notify.
    // This is what KConfigWatcher subscribes to, and KWin's KeyboardLayout is
    // one of its subscribers — see the header for why the more obvious
    // org.kde.KWin.reconfigure is not.
    QDBusMessage msg = QDBusMessage::createSignal(QStringLiteral("/kxkbrc"),
                                                  QStringLiteral("org.kde.kconfig.notify"),
                                                  QStringLiteral("ConfigChanged"));
    msg << QVariant::fromValue(payload);
    QDBusConnection::sessionBus().send(msg);
}

void KeyboardControl::apply(bool force)
{
    if (!enabled())
        return;

    const auto pending = pendingWrites();
    if (pending.isEmpty())
        return;

    const QString path = kxkbrcPath();
    const bool haveFile = !path.isEmpty() && QFile::exists(path);
    QSettings kxkb(path, QSettings::IniFormat);
    if (haveFile)
        kxkb.sync();

    const QString tool = findKwriteconfig();
    QStringList written;
    for (const Pending &p : pending) {
        const bool present = haveFile && kxkb.contains(p.key);
        if (p.remove) {
            if (!present)
                continue; // already absent: nothing to delete, nothing to notify
        } else if (present && kxkb.value(p.key).toString() == p.value) {
            continue; // identical, and an identical write would only churn the file
        }
        // kwriteconfig6 and not QSettings, for the reason AppearanceControl and
        // QtCompat::syncKdeUiSettings document: kxkbrc is a KConfig file, and a
        // round trip through a parser that never heard of [$i] or localised
        // keys is not worth the two processes it saves.
        if (tool.isEmpty())
            break;
        const QString group = p.key.section(QLatin1Char('/'), 0, 0);
        const QString key = p.key.section(QLatin1Char('/'), 1);
        QStringList args{QStringLiteral("--file"),  QStringLiteral("kxkbrc"),
                         QStringLiteral("--group"), group,
                         QStringLiteral("--key"),   key};
        if (p.remove)
            args << QStringLiteral("--delete");
        else
            args << p.value;
        // Synchronous, like the [UiSettings] write of QtCompat: the whole point
        // is that the layout is right *before* the user starts typing, and a
        // detached write leaves a window where it is not. Costs ~30 ms per key,
        // and only keys that actually moved are written.
        QProcess proc;
        proc.start(tool, args);
        if (proc.waitForFinished(1200))
            written << key;
    }

    if (written.isEmpty() && !force)
        return;
    // With nothing written, `force` still notifies: the case the tab's button
    // exists for is KWin and kxkbrc having drifted apart, and then there is
    // nothing to write but everything to reload.
    notifyKWin(written.isEmpty()
                   ? QStringList{QStringLiteral("LayoutList"), QStringLiteral("VariantList"),
                                 QStringLiteral("Model"), QStringLiteral("Options")}
                   : written);
    emit changed();
}

void KeyboardControl::refreshActive()
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                       QStringLiteral("/Layouts"),
                                                       QStringLiteral("org.kde.KeyboardLayouts"),
                                                       QStringLiteral("getLayoutsList"));
    // Asynchronous on purpose. This runs from the settings dialog, but a
    // blocking call to a compositor that is busy is exactly the shape of the
    // 25 s D-Bus timeout that froze the dock's startup (2026-08-21).
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(call, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                m_answered = true;
                m_active.clear();
                const QDBusMessage reply = w->reply();
                if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
                    emit activeChanged();
                    return;
                }
                // a(sss) — demarshalled by hand rather than through a
                // registered struct: it is read in one place and the struct
                // would have to be a metatype for no other reason.
                const QDBusArgument arg = reply.arguments().constFirst().value<QDBusArgument>();
                arg.beginArray();
                while (!arg.atEnd()) {
                    ActiveLayout l;
                    arg.beginStructure();
                    arg >> l.name >> l.variant >> l.displayName;
                    arg.endStructure();
                    m_active.append(l);
                }
                arg.endArray();
                emit activeChanged();
            });
}
