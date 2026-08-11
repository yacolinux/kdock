#include "weatherwindow.h"

#include "dockconfig.h"
#include "iconprovider.h"
#include "theme.h"
#include "weatherconfig.h"
#include "weathercontrol.h"
#include "weathersettingsdialog.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScreen>
#include <QSettings>

WeatherWindow::WeatherWindow(WeatherConfig *config, WeatherControl *weather, Theme *theme)
    : m_config(config)
    , m_weather(weather)
    , m_theme(theme)
{
    setTitle(QStringLiteral("kdock Weather"));
    // The QML paints its own background, so the surface has to show through.
    setColor(Qt::transparent);
    // Unlike the tile menu (which the compositor maximizes), this one has a size
    // of its own and the QML fills it.
    setResizeMode(QQuickView::SizeRootObjectToView);
    applySize();
    if (m_config)
        connect(m_config, &WeatherConfig::changed, this, &WeatherWindow::applySize);

    engine()->addImageProvider(QStringLiteral("icon"), new IconProvider);
    rootContext()->setContextProperty(QStringLiteral("theme"), m_theme);
    rootContext()->setContextProperty(QStringLiteral("weather"), m_weather);
    rootContext()->setContextProperty(QStringLiteral("weatherConfig"), m_config);
    rootContext()->setContextProperty(QStringLiteral("win"), this);

    setSource(QUrl(QStringLiteral("qrc:/qml/Weather.qml")));
}

void WeatherWindow::applySize()
{
    const qreal scale = m_config ? m_config->fontScale() : 1.0;
    int w = qRound(500 * scale);
    int h = qRound(470 * scale);
    if (QScreen *s = screen() ? screen() : QGuiApplication::primaryScreen()) {
        const QRect avail = s->availableGeometry();
        w = qMin(w, avail.width());
        h = qMin(h, avail.height());
    }
    if (w != width() || h != height())
        resize(w, h);
}

void WeatherWindow::showOn(const QString &screenName)
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
    if (target && screen() != target)
        setScreen(target);

    // Centred by hand: under X this is what places it, and under Wayland the
    // compositor overrides it (a client cannot place a toplevel there).
    if (QScreen *s = screen() ? screen() : QGuiApplication::primaryScreen()) {
        const QRect avail = s->availableGeometry();
        setPosition(avail.center() - QPoint(width() / 2, height() / 2));
    }

    show();
    requestActivate();
    // Coming back to a window that has been down for a while should not show
    // yesterday's weather while it waits for the timer.
    if (m_weather)
        m_weather->refresh(false);
}

void WeatherWindow::closeWindow()
{
    hide();
    // Single instance but not resident: nothing of this binary outlives its
    // window, so an install is never shadowed by an old process still mapped.
    QCoreApplication::quit();
}

bool WeatherWindow::event(QEvent *e)
{
    if (e->type() == QEvent::Close) {
        QCoreApplication::quit();
        return true;
    }
    return QQuickView::event(e);
}

void WeatherWindow::openSettings()
{
    if (!m_settingsDialog)
        m_settingsDialog = new WeatherSettingsDialog(m_config, m_weather);
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

QString WeatherWindow::iconSuffix() const
{
    // Same rule as CmWindow::iconSuffix(): revision for cache busting, plus the
    // icon set that reads over *this* window's background. Breeze's weather
    // icons are the exact case that made this necessary — several are dark
    // line-art and vanish on a dark panel.
    QString suffix = QStringLiteral("@") + QString::number(m_theme ? m_theme->revision() : 0);

    const QColor base = m_theme ? m_theme->background() : QColor(Qt::black);
    const qreal luma = 0.299 * base.redF() + 0.587 * base.greenF() + 0.114 * base.blueF();

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

void WeatherWindow::retranslate()
{
    engine()->retranslate();
    if (!m_settingsDialog)
        return;
    const bool wasVisible = m_settingsDialog->isVisible();
    m_settingsDialog->deleteLater();
    m_settingsDialog = nullptr;
    if (wasVisible)
        openSettings();
}
