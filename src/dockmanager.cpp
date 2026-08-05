#include "dockmanager.h"

#include "clockwidget.h"
#include "clockwidget2.h"
#include "dockconfig.h"
#include "dockmodel.h"
#include "dockwindow.h"
#include "systraymodel.h"

#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>

#include <algorithm>

DockManager::DockManager(const Shared &shared, QObject *parent)
    : QObject(parent)
    , m_shared(shared)
{
    migrateFirstRun();
    normalizeSystrayOwner();

    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this] { sync(); });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] { sync(); });
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, [this] { sync(); });

    sync();
}

QString DockManager::primaryScreenName() const
{
    QScreen *p = QGuiApplication::primaryScreen();
    return p ? p->name() : QString();
}

QString DockManager::primaryDockId() const
{
    // The primary dock (systray/relanzadores host) is the lowest-slot enabled
    // dock on the primary monitor. Empty if the primary has no enabled dock.
    const QStringList docks = enabledDocksForScreen(primaryScreenName());
    return docks.isEmpty() ? QString() : docks.first();
}

QString DockManager::systrayDockId()
{
    // Only one dock draws the tray at a time (see normalizeSystrayOwner): the
    // first configured dock that claims it wins, so the answer is stable even if
    // a hand-edited config flags several.
    for (const QString &id : configuredDocks())
        if (configFor(id)->showSystray())
            return id;
    return QString();
}

void DockManager::normalizeSystrayOwner()
{
    // The tray used to be drawn by the primary dock only, so "showSystray" could
    // be left true in several per-dock files without any visible effect. Now
    // every dock honours its own flag, which would show the same items twice, so
    // the extra claims are dropped once at startup. The primary dock keeps the
    // tray when it has it, to preserve the pre-change appearance.
    const QString preferred = primaryDockId();
    QString owner = (!preferred.isEmpty() && configFor(preferred)->showSystray())
                        ? preferred
                        : QString();
    for (const QString &id : configuredDocks()) {
        DockConfig *cfg = configFor(id);
        if (!cfg->showSystray())
            continue;
        if (owner.isEmpty())
            owner = id;
        else if (id != owner)
            cfg->setShowSystray(false);
    }
}

QStringList DockManager::connectedScreens() const
{
    QStringList names;
    const auto screens = QGuiApplication::screens();
    names.reserve(screens.size());
    for (QScreen *s : screens)
        names << s->name();
    return names;
}

QStringList DockManager::enabledDocksForScreen(const QString &screenName) const
{
    QStringList ids;
    for (const QString &id : DockConfig::enabledDocks())
        if (DockConfig::screenOfDockId(id) == screenName)
            ids << id;
    std::sort(ids.begin(), ids.end(), [](const QString &a, const QString &b) {
        return DockConfig::slotOfDockId(a) < DockConfig::slotOfDockId(b);
    });
    return ids;
}

bool DockManager::isDockEnabled(const QString &dockId) const
{
    return DockConfig::enabledDocks().contains(dockId);
}

void DockManager::setDockEnabled(const QString &dockId, bool enabled)
{
    // When enabling a non-first dock for the first time, seed its config with a
    // screen edge not already used by the other docks on the same monitor so
    // they don't overlap.
    if (enabled && !isDockEnabled(dockId) && DockConfig::slotOfDockId(dockId) > 0) {
        const QString screen = DockConfig::screenOfDockId(dockId);
        QList<int> used;
        for (const QString &id : enabledDocksForScreen(screen))
            used << configFor(id)->edge();
        for (int edge : {DockConfig::Bottom, DockConfig::Top, DockConfig::Left,
                         DockConfig::Right}) {
            if (!used.contains(edge)) {
                configFor(dockId)->setEdge(edge);
                break;
            }
        }
    }
    if (enabled)
        DockConfig::addKnownDock(dockId);
    DockConfig::setDockEnabled(dockId, enabled);
    sync();
}

QStringList DockManager::configuredDocks() const
{
    QStringList ids = DockConfig::enabledDocks();
    for (const QString &id : DockConfig::knownDocks())
        if (!ids.contains(id))
            ids << id;
    std::sort(ids.begin(), ids.end(), [](const QString &a, const QString &b) {
        const QString sa = DockConfig::screenOfDockId(a);
        const QString sb = DockConfig::screenOfDockId(b);
        if (sa != sb)
            return sa < sb;
        return DockConfig::slotOfDockId(a) < DockConfig::slotOfDockId(b);
    });
    return ids;
}

QStringList DockManager::knownScreensForUi() const
{
    QStringList screens = DockConfig::knownScreens();
    const auto add = [&screens](const QString &s) {
        if (!s.isEmpty() && !screens.contains(s))
            screens << s;
    };
    for (const QString &s : connectedScreens())
        add(s);
    for (const QString &id : configuredDocks())
        add(DockConfig::screenOfDockId(id));
    return screens;
}

int DockManager::firstFreeSlot(const QString &screenName) const
{
    QList<int> used;
    for (const QString &id : configuredDocks())
        if (DockConfig::screenOfDockId(id) == screenName)
            used << DockConfig::slotOfDockId(id);
    for (int slot = 0; slot < DockConfig::kMaxDocksPerScreen; ++slot)
        if (!used.contains(slot))
            return slot;
    return -1;
}

int DockManager::firstFreeSlotWithPreviews(const QString &screenName) const
{
    QList<int> used;
    for (const QString &id : configuredDocks())
        if (DockConfig::screenOfDockId(id) == screenName)
            used << DockConfig::slotOfDockId(id);
    for (const QString &dst : m_previewSource.keys())
        if (DockConfig::screenOfDockId(dst) == screenName)
            used << DockConfig::slotOfDockId(dst);
    for (int slot = 0; slot < DockConfig::kMaxDocksPerScreen; ++slot)
        if (!used.contains(slot))
            return slot;
    return -1;
}

bool DockManager::previewDockOnScreen(const QString &srcDockId,
                                      const QString &targetScreen, QString *error)
{
    const int slot = firstFreeSlotWithPreviews(targetScreen);
    if (slot < 0) {
        if (error)
            *error = tr("El monitor %1 ya tiene el máximo de docks (%2).")
                         .arg(targetScreen)
                         .arg(DockConfig::kMaxDocksPerScreen);
        return false;
    }
    const QString dstDockId = DockConfig::makeDockId(targetScreen, slot);

    // The destination slot is free (see firstFreeSlotWithPreviews): no live or
    // configured dock and no pending preview sit on it, so anything already at
    // its file path is a stale leftover from a deleted/disabled dock. Reusing
    // that file would make the preview show the old config (or a default stub)
    // instead of the source's — the "solo muestra el dock por default" bug.
    // Always (re)write it from the source's *live* settings — not a raw file
    // copy, which would miss settings a freshly enabled dock has not flushed to
    // disk yet, and would drag the source's screenName along. The alias is
    // skipped, so a copy starts unnamed (see copySettingsTo).
    if (DockConfig *cfg = m_configs.take(dstDockId))
        delete cfg; // defensive: a leaked config would be stale
    if (!configFor(srcDockId)->copySettingsTo(dstDockId)) {
        if (error)
            *error = tr("No se pudo crear la copia de \"%1\" en %2.")
                         .arg(srcDockId, targetScreen);
        return false;
    }
    m_previewCreatedFile.insert(dstDockId);

    m_previewSource.insert(dstDockId, srcDockId);
    // Show a real preview window only when the target screen is connected;
    // otherwise the preview is just staged and materializes on apply/hotplug.
    if (connectedScreens().contains(targetScreen))
        m_previews.insert(dstDockId, buildInstance(dstDockId, false));
    return true;
}

void DockManager::dropPreview(const QString &dstDockId, bool deleteFileIfCreated)
{
    auto it = m_previews.find(dstDockId);
    if (it != m_previews.end()) {
        teardownInstance(it.value());
        m_previews.erase(it);
    }
    if (deleteFileIfCreated && m_previewCreatedFile.contains(dstDockId)) {
        QFile::remove(DockConfig::instanceSettingsFilePath(dstDockId));
        // Drop the transient config object bound to the now-deleted file.
        if (DockConfig *cfg = m_configs.take(dstDockId))
            delete cfg;
    }
    m_previewCreatedFile.remove(dstDockId);
    m_previewSource.remove(dstDockId);
}

void DockManager::unpreviewScreen(const QString &srcDockId, const QString &targetScreen)
{
    for (const QString &dst : m_previewSource.keys()) {
        if (m_previewSource.value(dst) == srcDockId
            && DockConfig::screenOfDockId(dst) == targetScreen) {
            dropPreview(dst, /*deleteFileIfCreated=*/true);
            return;
        }
    }
}

void DockManager::clearPreviews()
{
    for (const QString &dst : m_previewSource.keys())
        dropPreview(dst, /*deleteFileIfCreated=*/true);
}

void DockManager::applyPreviews(const QString &srcDockId)
{
    const QStringList dsts = m_previewSource.keys();
    bool applied = false;
    for (const QString &dst : dsts) {
        if (m_previewSource.value(dst) != srcDockId)
            continue;
        // Persist without edge-seeding so the copy keeps the source's edge.
        DockConfig::addKnownDock(dst);
        DockConfig::setDockEnabled(dst, true);
        // Keep the file; drop only the preview bookkeeping (the instance, if
        // any, is rebuilt by sync() with the correct primary role).
        dropPreview(dst, /*deleteFileIfCreated=*/false);
        applied = true;
    }
    if (applied)
        sync();
}

bool DockManager::hasPreview(const QString &srcDockId, const QString &screen) const
{
    for (auto it = m_previewSource.constBegin(); it != m_previewSource.constEnd(); ++it)
        if (it.value() == srcDockId && DockConfig::screenOfDockId(it.key()) == screen)
            return true;
    return false;
}

bool DockManager::hasPreviewsFor(const QString &srcDockId) const
{
    return m_previewSource.values().contains(srcDockId);
}

void DockManager::removeDock(const QString &dockId)
{
    // Tear down the live dock before touching its config file: the DockWindow
    // holds a pointer to the cached DockConfig we are about to delete.
    destroyInstance(dockId);
    // Discard any pending previews that were sourced from this dock.
    for (const QString &dst : m_previewSource.keys()) {
        if (m_previewSource.value(dst) == dockId)
            dropPreview(dst, /*deleteFileIfCreated=*/true);
    }
    DockConfig::removeKnownDock(dockId);
    DockConfig::setDockEnabled(dockId, false);

    // Delete the per-dock config file so re-enabling the same monitor+slot
    // starts fresh instead of resurrecting the removed dock's settings, and
    // drop the cached config object bound to the deleted file.
    QFile::remove(DockConfig::instanceSettingsFilePath(dockId));
    if (DockConfig *cfg = m_configs.take(dockId))
        delete cfg;
    sync();
}

DockConfig *DockManager::configFor(const QString &dockId)
{
    auto it = m_configs.find(dockId);
    if (it != m_configs.end())
        return it.value();
    auto *cfg = new DockConfig(dockId, this);
    m_configs.insert(dockId, cfg);
    return cfg;
}

void DockManager::migrateFirstRun()
{
    // First run under the multi-instance scheme: no monitor has been enabled
    // yet. Adopt the primary monitor, seeding its per-screen file from the old
    // shared config so the existing single-dock setup carries over unchanged.
    if (!DockConfig::enabledDocks().isEmpty())
        return;

    const QString primary = primaryScreenName();
    if (primary.isEmpty())
        return;

    // Slot 0 of the primary keeps the legacy per-monitor file name.
    const QString dest = DockConfig::instanceSettingsFilePath(primary);
    if (!QFileInfo::exists(dest))
        QFile::copy(DockConfig::settingsFilePath(), dest);

    DockConfig::setDockEnabled(primary, true);
}

void DockManager::sync()
{
    const QString primaryDock = primaryDockId();
    const QStringList connected = connectedScreens();

    // Remember every connected monitor so the settings UI can still offer it
    // once it is unplugged.
    for (const QString &screen : connected)
        DockConfig::addKnownScreen(screen);

    // A dock is wanted where it is enabled and its monitor is connected.
    QStringList wanted;
    for (const QString &id : DockConfig::enabledDocks())
        if (connected.contains(DockConfig::screenOfDockId(id)))
            wanted << id;

    // Tear down docks no longer wanted, or whose primary role changed (the
    // primary dock is the only one that hosts the systray/relanzadores, so a
    // change of primary monitor means recreating the affected instances).
    const QStringList shown = m_instances.keys();
    for (const QString &id : shown) {
        const bool wantPrimary = (id == primaryDock);
        if (!wanted.contains(id) || m_instances[id].primary != wantPrimary)
            destroyInstance(id);
    }

    // Bring up any wanted dock that is not shown yet.
    for (const QString &id : wanted)
        if (!m_instances.contains(id))
            createInstance(id, id == primaryDock);
}

void DockManager::createInstance(const QString &dockId, bool primary)
{
    m_instances.insert(dockId, buildInstance(dockId, primary));
}

DockManager::Instance DockManager::buildInstance(const QString &dockId, bool primary)
{
    DockConfig *cfg = configFor(dockId);

    auto *model = new DockModel(cfg, m_shared.apps, m_shared.monitor, this);
    // Every dock gets its own view of the shared tray host: the model is a
    // filtered list over SystrayHost::items(), so several are harmless. Which
    // dock actually draws it is the per-dock "showSystray" flag, gated in QML —
    // keeping it out of the instance role means toggling the tray never has to
    // tear down and rebuild a dock window.
    auto *systrayModel = new SystrayModel(m_shared.systrayHost, cfg, this);

    // Per-dock clocks, bound to this monitor's format settings.
    auto *clock = new ClockWidget(this);
    auto *clock2 = new ClockWidget2(this);
    // The click command may name a .desktop id; resolve it through the shared
    // app index (see ClockWidget2::launch).
    clock2->setApps(m_shared.apps);
    const auto bindClocks = [cfg, clock, clock2] {
        clock->setFormat24h(cfg->clockFormat24h());
        clock->setShowDate(cfg->clockShowDate());
        clock->setShowSeconds(cfg->clockShowSeconds());
        clock2->setFormat24h(cfg->clockFormat24h());
        clock2->setShowDate(cfg->clockShowDate());
        clock2->setShowSeconds(cfg->clockShowSeconds());
        clock2->setCommand(cfg->clock2Command());
    };
    bindClocks();
    connect(cfg, &DockConfig::clockFormat24hChanged, clock, bindClocks);
    connect(cfg, &DockConfig::clockShowDateChanged, clock, bindClocks);
    connect(cfg, &DockConfig::clockShowSecondsChanged, clock, bindClocks);
    connect(cfg, &DockConfig::clock2CommandChanged, clock, bindClocks);

    auto *window = new DockWindow(
        cfg, m_shared.theme, model, m_shared.apps, m_shared.volume, clock,
        clock2, m_shared.brightness, m_shared.battery, m_shared.overview, m_shared.desktopControl,
        m_shared.monitorControl, m_shared.maxmin, m_shared.activeWindow, m_shared.wallpaperControl, m_shared.power, systrayModel, m_shared.systrayHost,
        m_shared.relanzadores, m_shared.scriptRunners, m_shared.clipboardHistory,
        m_shared.disks, m_shared.network, m_shared.appearance, m_shared.monitor);
    window->setManager(this);
    window->setPrimary(primary);
    window->show();

    return {model, systrayModel, clock, clock2, window, primary};
}

void DockManager::teardownInstance(Instance &inst)
{
    // Delete the per-instance objects we created explicitly.
    delete inst.window;
    delete inst.systrayModel;
    delete inst.clock2;
    delete inst.clock;
    delete inst.model;
    inst = {};
    // The DockConfig stays cached in m_configs for reuse/settings editing.
}

DockModel *DockManager::modelFor(const QString &dockId) const
{
    const auto it = m_instances.constFind(dockId);
    return it == m_instances.constEnd() ? nullptr : it.value().model;
}

void DockManager::destroyInstance(const QString &dockId)
{
    auto it = m_instances.find(dockId);
    if (it == m_instances.end())
        return;
    Instance inst = it.value();
    m_instances.erase(it);
    teardownInstance(inst);
}
