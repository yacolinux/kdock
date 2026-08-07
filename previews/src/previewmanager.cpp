#include "previewmanager.h"

#include "kwinwindows.h"
#include "previewconfig.h"
#include "previewmodel.h"
#include "previewsettingsdialog.h"
#include "previewwindow.h"
#include "screenshotsource.h"
#include "thumbnailcache.h"
#include "virtualdesktops.h"

#include "desktopentry.h"
#include "theme.h"

#include <QGuiApplication>
#include <QQmlEngine>
#include <QScreen>
#include <QTimer>

PreviewManager::PreviewManager(QObject *parent)
    : QObject(parent)
    , m_theme(new Theme(this))
    , m_apps(new DesktopEntryIndex(this))
    , m_windows(nullptr)
    , m_desktops(new VirtualDesktops(this))
    , m_cache(new ThumbnailCache(this))
    , m_source(new ScreenShotSource(this))
{
    // A QWaylandClientExtension may only be built on the Wayland platform (kdock
    // guards its window monitor the same way): under xcb/offscreen the strips
    // simply stay empty, which is enough for a QML smoke test.
    if (QGuiApplication::platformName().contains(QLatin1String("wayland"))) {
        m_windows = new KWinWindows(this);
        connect(m_windows, &KWinWindows::windowRemoved, this, [this](KWinWindow *w) {
            m_source->cancel(w->uuid());
            m_cache->forget(w->uuid());
            m_captureFailWarned.remove(w->uuid());
        });
    } else {
        qWarning("kdock-previews: not running on Wayland; no windows will be listed.");
    }

    // The cache has a single writer, here, so several strips showing the same
    // window share one capture and one revision counter.
    connect(m_source, &ScreenShotSource::thumbnailReady, m_cache, &ThumbnailCache::store);
    connect(m_source, &ScreenShotSource::thumbnailFailed, this,
            [this](const QString &uuid, const QString &reason) {
                // Stamped so the scheduler waits a full interval before retrying.
                m_cache->markAttempt(uuid);
                // A card that stays on its app icon is otherwise unexplainable
                // from the outside: say why, once per window.
                if (m_captureFailWarned.contains(uuid))
                    return;
                m_captureFailWarned.insert(uuid);
                qWarning("kdock-previews: capture of %s failed, the card keeps the app "
                         "icon: %s", qPrintable(uuid), qPrintable(reason));
            });

    connect(qGuiApp, &QGuiApplication::screenAdded, this, &PreviewManager::sync);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &PreviewManager::sync);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &PreviewManager::sync);

    migrateFirstRun();
    sync();
}

ThumbnailSource *PreviewManager::source() const
{
    return m_source;
}

QString PreviewManager::primaryScreenName() const
{
    QScreen *primary = QGuiApplication::primaryScreen();
    return primary ? primary->name() : QString();
}

QStringList PreviewManager::connectedScreens() const
{
    QStringList names;
    const auto screens = QGuiApplication::screens();
    for (QScreen *s : screens)
        names << s->name();
    return names;
}

QStringList PreviewManager::knownScreensForUi() const
{
    QStringList names = PreviewConfig::knownScreens();
    for (const QString &name : connectedScreens()) {
        if (!names.contains(name))
            names << name;
    }
    for (const QString &name : PreviewConfig::enabledScreens()) {
        if (!names.contains(name))
            names << name;
    }
    names.sort();
    return names;
}

bool PreviewManager::enabled() const
{
    return PreviewConfig::previewsEnabled();
}

void PreviewManager::setEnabled(bool enabled)
{
    PreviewConfig::setPreviewsEnabled(enabled);
    sync();
}

void PreviewManager::reload()
{
    sync();
}

bool PreviewManager::isScreenEnabled(const QString &screenName) const
{
    return PreviewConfig::enabledScreens().contains(screenName);
}

void PreviewManager::setScreenEnabled(const QString &screenName, bool enabled)
{
    PreviewConfig::setScreenEnabled(screenName, enabled);
    sync();
}

PreviewConfig *PreviewManager::configFor(const QString &screenName)
{
    if (auto *existing = m_configs.value(screenName))
        return existing;
    auto *config = new PreviewConfig(screenName, this);
    m_configs.insert(screenName, config);
    return config;
}

void PreviewManager::migrateFirstRun()
{
    // Nothing opted in yet: adopt the primary monitor, otherwise the binary
    // starts, shows nothing, and looks broken.
    if (!PreviewConfig::enabledScreens().isEmpty())
        return;
    const QString primary = primaryScreenName();
    if (primary.isEmpty())
        return;
    PreviewConfig::setScreenEnabled(primary, true);
}

void PreviewManager::sync()
{
    const QStringList connected = connectedScreens();
    for (const QString &screen : connected)
        PreviewConfig::addKnownScreen(screen);

    // Master switch off: no strips at all (but the settings panel still works,
    // which is what `--settings` on a disabled setup relies on).
    QStringList wanted;
    if (PreviewConfig::previewsEnabled()) {
        for (const QString &screen : PreviewConfig::enabledScreens()) {
            if (connected.contains(screen))
                wanted << screen;
        }
    }

    const QStringList shown = m_instances.keys();
    for (const QString &screen : shown) {
        if (!wanted.contains(screen))
            destroyStrip(screen);
    }
    for (const QString &screen : wanted) {
        if (!m_instances.contains(screen))
            createStrip(screen);
    }
}

void PreviewManager::createStrip(const QString &screenName)
{
    PreviewConfig *config = configFor(screenName);

    Instance inst;
    inst.model = new PreviewModel(config, m_windows, m_desktops, m_apps, m_cache, m_source, this);
    inst.window = new PreviewWindow(config, m_theme, inst.model, m_cache, m_apps);
    inst.window->setManager(this);
    inst.window->show();

    m_instances.insert(screenName, inst);

    // KWin's window_with_uuid burst for the already-open windows is still in the
    // socket buffer at this point, and each window only surfaces once its initial
    // state is complete. Staggered re-syncs pick up whatever was still settling
    // (same reason DockWindow does it).
    for (int delay : {200, 700, 1500}) {
        QTimer::singleShot(delay, inst.model, [model = inst.model] { model->sync(); });
    }
}

void PreviewManager::destroyStrip(const QString &screenName)
{
    Instance inst = m_instances.take(screenName);
    if (inst.window) {
        inst.window->hide();
        // Drop the QML tree *now*, while the context properties it is bound to
        // are still alive. Left to the destructor, the bindings re-evaluate
        // against a half-dismantled context and log a burst of
        // "Cannot read property 'x' of null" (seen 2026-07-30 when toggling the
        // strip on and off).
        inst.window->setSource(QUrl());
        inst.window->deleteLater();
    }
    if (inst.model)
        inst.model->deleteLater();
}

void PreviewManager::retranslate()
{
    for (Instance &inst : m_instances) {
        if (inst.window)
            inst.window->engine()->retranslate();
    }
    if (m_dialog) {
        const bool wasVisible = m_dialog->isVisible();
        m_dialog->deleteLater();
        m_dialog = nullptr;
        if (wasVisible)
            showSettings();
    }
}

void PreviewManager::showSettings()
{
    if (!m_dialog)
        m_dialog = new PreviewSettingsDialog(this);
    m_dialog->show();
    m_dialog->raise();
    m_dialog->activateWindow();
}
