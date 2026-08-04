#include "tilewindow.h"

#include "appmenu.h"
#include "desktopentry.h"
#include "iconpickerdialog.h"
#include "iconprovider.h"
#include "powercontrol.h"
#include "theme.h"
#include "tileconfig.h"
#include "tilelayout.h"
#include "tilemodel.h"
#include "tilesettingsdialog.h"

#include <QColorDialog>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScreen>

namespace {
// Focus can bounce for a frame or two while the compositor maps the window.
constexpr qint64 kFocusGraceMs = 400;
} // namespace

TileWindow::TileWindow(TileConfig *config, Theme *theme, DesktopEntryIndex *apps, AppMenu *menu,
                       TileLayout *layout, TileModel *model, PowerControl *power)
    : m_config(config)
    , m_theme(theme)
    , m_apps(apps)
    , m_menu(menu)
    , m_layout(layout)
    , m_model(model)
    , m_power(power)
{
    setTitle(QStringLiteral("kdock Tile Menu"));
    // Frameless because a menu has no business carrying a title bar; the
    // maximized state is independent of the decoration.
    setFlags(Qt::Window | Qt::FramelessWindowHint);
    // The QML background paints its own color at config.backgroundOpacity, so
    // the window surface itself has to be able to show through.
    setColor(Qt::transparent);
    // The compositor dictates the size here (the opposite of the dock, which
    // sizes its surface from its content).
    setResizeMode(QQuickView::SizeRootObjectToView);

    engine()->addImageProvider(QStringLiteral("icon"), new IconProvider);
    rootContext()->setContextProperty(QStringLiteral("theme"), m_theme);
    rootContext()->setContextProperty(QStringLiteral("tileConfig"), m_config);
    rootContext()->setContextProperty(QStringLiteral("appMenu"), m_menu);
    rootContext()->setContextProperty(QStringLiteral("tileLayout"), m_layout);
    rootContext()->setContextProperty(QStringLiteral("tiles"), m_model);
    rootContext()->setContextProperty(QStringLiteral("power"), m_power);
    rootContext()->setContextProperty(QStringLiteral("win"), this);

    setSource(QUrl(QStringLiteral("qrc:/qml/TileMenu.qml")));

    connect(this, &QWindow::activeChanged, this, &TileWindow::onActiveChanged);
}

void TileWindow::showOn(const QString &screenName)
{
    QScreen *target = nullptr;
    if (!screenName.isEmpty()) {
        const auto screens = QGuiApplication::screens();
        for (QScreen *s : screens) {
            if (s->name() == screenName) {
                target = s;
                break;
            }
        }
    }
    // setScreen() only counts while the platform window does not exist yet, and
    // even then Wayland treats it as a hint. Doing it with the window down is
    // the most we can ask for.
    if (target && screen() != target) {
        if (!isVisible() && handle())
            destroy();
        setScreen(target);
    }

    m_shownAt = QDateTime::currentMSecsSinceEpoch();

    // "Remember the section" off means every open starts at the first sidebar
    // row (Favorites); an empty remembered value means we have never run.
    QString section = m_config->rememberSection() ? m_config->lastSection() : QString();
    if (section.isEmpty() && m_menu) {
        const QVariantList sections = m_menu->sections();
        if (!sections.isEmpty())
            section = sections.first().toMap().value(QStringLiteral("key")).toString();
    }
    m_model->setSection(section);
    m_model->refresh();

    // Belt and braces. On Wayland the compositor's configure decides and this
    // request is ignored, which is the whole point of maximizing: KWin already
    // subtracts the docks' struts. The explicit geometry is what makes the
    // window fill the screen under a bare X server with no window manager at
    // all — i.e. the Xvfb harness, where showMaximized() has nobody to honour
    // it and the view would come up at its default 160x160.
    if (QScreen *s = screen())
        setGeometry(s->availableGeometry());
    showMaximized();
    raise();
    requestActivate();
}

void TileWindow::hideMenu()
{
    // hide(), never destroy(): the whole point of staying resident is that the
    // next click is instant.
    hide();
}

bool TileWindow::blockingClose() const
{
    if (m_modalDepth > 0)
        return true;
    return m_settingsDialog && m_settingsDialog->isVisible();
}

void TileWindow::onActiveChanged()
{
    if (isActive() || !isVisible())
        return;
    if (m_config->keepOpen() || !m_config->closeOnFocusLoss())
        return;
    if (blockingClose())
        return;
    if (QDateTime::currentMSecsSinceEpoch() - m_shownAt < kFocusGraceMs)
        return;
    hideMenu();
}

void TileWindow::launch(const QString &id)
{
    if (m_menu)
        m_menu->launch(id);
    if (m_config->closeOnLaunch() && !m_config->keepOpen())
        hideMenu();
}

void TileWindow::openSettings()
{
    if (!m_settingsDialog)
        m_settingsDialog = new TileSettingsDialog(m_config, m_layout, m_menu);
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void TileWindow::quitApp()
{
    QCoreApplication::quit();
}

QString TileWindow::pickIcon(const QString &current)
{
    ++m_modalDepth;
    IconPickerDialog dlg(current);
    const bool ok = dlg.exec() == QDialog::Accepted;
    --m_modalDepth;
    requestActivate();
    return ok ? dlg.selectedIcon() : QString();
}

QString TileWindow::pickColor(const QString &current)
{
    ++m_modalDepth;
    const QColor chosen = QColorDialog::getColor(QColor(current), nullptr,
                                                 tr("Color del mosaico"));
    --m_modalDepth;
    requestActivate();
    return chosen.isValid() ? chosen.name() : QString();
}

QString TileWindow::pickImage(const QString &current)
{
    ++m_modalDepth;
    const QString path = QFileDialog::getOpenFileName(
        nullptr, tr("Imagen de fondo del mosaico"), current,
        tr("Imágenes (*.png *.jpg *.jpeg *.webp *.bmp *.svg);;Todos los archivos (*)"));
    --m_modalDepth;
    requestActivate();
    return path;
}

QString TileWindow::promptText(const QString &title, const QString &label, const QString &current)
{
    ++m_modalDepth;
    bool ok = false;
    const QString text = QInputDialog::getText(nullptr, title, label, QLineEdit::Normal, current,
                                               &ok);
    --m_modalDepth;
    requestActivate();
    return ok ? text : QString();
}

bool TileWindow::confirm(const QString &title, const QString &text)
{
    ++m_modalDepth;
    const auto answer = QMessageBox::question(nullptr, title, text,
                                              QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::No);
    --m_modalDepth;
    requestActivate();
    return answer == QMessageBox::Yes;
}
