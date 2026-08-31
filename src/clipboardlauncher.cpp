#include "clipboardlauncher.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {
const auto kBinary = QStringLiteral("kdock-clipboard");
const auto kService = QStringLiteral("org.kdock.Clipboard");
const auto kPath = QStringLiteral("/Clipboard");

void callClipboard(const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kService, method);
    if (!args.isEmpty())
        msg.setArguments(args);
    // A wedged clipboard process must never make the dock wait for a D-Bus
    // reply. The command is fire-and-forget by design.
    QDBusConnection::sessionBus().asyncCall(msg);
}
} // namespace

QString ClipboardLauncher::binaryPath()
{
    const QString dir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        dir + QLatin1Char('/') + kBinary,
        dir + QStringLiteral("/clipboard/") + kBinary,
    };
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(kBinary);
}

bool ClipboardLauncher::installed()
{
    return !binaryPath().isEmpty();
}

bool ClipboardLauncher::running()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(kService);
}

void ClipboardLauncher::quitRunning()
{
    if (running())
        callClipboard(QStringLiteral("quit"));
}

bool ClipboardLauncher::start(const QStringList &args)
{
    const QString binary = binaryPath();
    if (binary.isEmpty())
        return false;
    // Detached: kdock and the clipboard window have independent lifetimes.
    return QProcess::startDetached(binary, args);
}

void ClipboardLauncher::toggle(const QString &screenName)
{
    if (running()) {
        callClipboard(QStringLiteral("toggle"), {screenName});
        return;
    }

    QStringList args{QStringLiteral("--toggle")};
    if (!screenName.isEmpty())
        args << QStringLiteral("--screen") << screenName;
    start(args);
}
