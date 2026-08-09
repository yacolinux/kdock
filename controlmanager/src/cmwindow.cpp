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
#include "wallpapercontrol.h"

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

    setTitle(QStringLiteral("kdock Control Manager"));
    setFlags(Qt::FramelessWindowHint);
    // The QML paints its own background at config.backgroundOpacity, so the
    // surface itself has to be able to show through.
    setColor(Qt::transparent);
    // The panel has a size of its own (unlike the dock, whose content dictates
    // it): we resize the view and the QML root fills it.
    setResizeMode(QQuickView::SizeRootObjectToView);

    // Layer-shell properties must be in place before the platform window exists
    // (i.e. before show()).
    setProperty("kdock.layershell", true);
    setProperty("kdock.keyboardInteractivity", 0u);
    applyLayerProperties();

    connect(m_config, &CmConfig::windowChanged, this, [this] {
        applyLayerProperties();
        applySize();
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
    rootContext()->setContextProperty(QStringLiteral("wallpaperControl"), m_backends.wallpaper);
    rootContext()->setContextProperty(QStringLiteral("power"), m_backends.power);
    rootContext()->setContextProperty(QStringLiteral("mpris"), m_backends.mpris);
    rootContext()->setContextProperty(QStringLiteral("dock"), m_backends.dock);

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
    QScreen *s = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect geo = s ? s->geometry() : QRect(0, 0, 1920, 1080);
    resize(m_config->panelWidthFor(geo.width()), m_config->panelHeightFor(geo.height()));
}

void CmWindow::applyLayerProperties()
{
    uint anchor = AnchorTop;
    QMargins margins;
    const int m = m_config->screenMargin();
    const bool horizontal = m_config->edge() == CmConfig::Top
                            || m_config->edge() == CmConfig::Bottom;
    switch (m_config->edge()) {
    case CmConfig::Top:    anchor = AnchorTop;    margins.setTop(m);    break;
    case CmConfig::Bottom: anchor = AnchorBottom; margins.setBottom(m); break;
    case CmConfig::Left:   anchor = AnchorLeft;   margins.setLeft(m);   break;
    case CmConfig::Right:  anchor = AnchorRight;  margins.setRight(m);  break;
    }

    // Alignment along the anchored edge. A single-edge anchor already centers
    // the surface, so Center adds nothing.
    if (m_config->alignment() == CmConfig::Start) {
        anchor |= horizontal ? AnchorLeft : AnchorTop;
        if (horizontal) margins.setLeft(m); else margins.setTop(m);
    } else if (m_config->alignment() == CmConfig::End) {
        anchor |= horizontal ? AnchorRight : AnchorBottom;
        if (horizontal) margins.setRight(m); else margins.setBottom(m);
    }

    setProperty("kdock.anchors", anchor);
    setProperty("kdock.margins", QVariant::fromValue(margins));
    setProperty("kdock.layer", 2u); // top
    // Always zero: this is a panel that comes and goes. Reserving space would
    // rearrange the user's maximized windows on every open.
    setProperty("kdock.exclusiveZone", 0);
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
    // Exclusive keyboard while up: Esc closes, and losing it is the "clicked
    // somewhere else" signal a layer surface has no other way to hear.
    setProperty("kdock.keyboardInteractivity", 1u);
    show();
    requestActivate();
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

void CmWindow::openSettings()
{
    if (!m_settingsDialog)
        m_settingsDialog = new CmSettingsDialog(m_config, m_layout, m_appearance);
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
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

QString CmWindow::pickColor(const QString &current)
{
    ++m_modalDepth;
    const QColor chosen = QColorDialog::getColor(QColor(current), nullptr,
                                                 tr("Color de la tarjeta"));
    --m_modalDepth;
    requestActivate();
    return chosen.isValid() ? chosen.name() : QString();
}

QString CmWindow::promptText(const QString &title, const QString &label, const QString &current)
{
    ++m_modalDepth;
    bool ok = false;
    const QString text = QInputDialog::getText(nullptr, title, label, QLineEdit::Normal, current,
                                               &ok);
    --m_modalDepth;
    requestActivate();
    return ok ? text : QString();
}

bool CmWindow::confirm(const QString &title, const QString &text)
{
    ++m_modalDepth;
    const auto answer = QMessageBox::question(nullptr, title, text,
                                              QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::No);
    --m_modalDepth;
    requestActivate();
    return answer == QMessageBox::Yes;
}
