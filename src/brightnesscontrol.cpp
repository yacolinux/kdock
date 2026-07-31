#include "brightnesscontrol.h"

#include "dockconfig.h"

#include <QDBusConnection>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

static const QString kBrightnessKey = QStringLiteral("Brightness/lastBrightness");

BrightnessControl::BrightnessControl(QObject *parent)
    : QObject(parent)
{
    m_brightnessctl = QStandardPaths::findExecutable(QStringLiteral("brightnessctl"));
    if (m_brightnessctl.isEmpty()) {
        qInfo("kdock: brightnessctl not found, brightness widget disabled");
        return;
    }

    // Recover the brightness saved before the last shutdown/suspend.
    {
        QSettings s(DockConfig::settingsFilePath(), QSettings::IniFormat);
        const qreal saved = s.value(kBrightnessKey, -1.0).toReal();
        if (saved >= MinBrightness && saved <= 1.0)
            m_restoreBrightness = saved;
    }

    // Restore brightness on resume from suspend/hibernate, where firmware
    // typically resets the backlight to full.
    QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.login1"),
                                         QStringLiteral("/org/freedesktop/login1"),
                                         QStringLiteral("org.freedesktop.login1.Manager"),
                                         QStringLiteral("PrepareForSleep"),
                                         this,
                                         SLOT(onPrepareForSleep(bool)));

    m_refreshDebounce.setSingleShot(true);
    m_refreshDebounce.setInterval(150);
    connect(&m_refreshDebounce, &QTimer::timeout, this, &BrightnessControl::refresh);
    refresh();

    // Re-apply the user's last brightness at startup (systemd-backlight may not
    // have, e.g. after a hard poweroff).
    if (m_restoreBrightness >= MinBrightness)
        setBrightness(m_restoreBrightness);
}

void BrightnessControl::refresh()
{
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p](int exitCode) {
        p->deleteLater();
        if (exitCode != 0) {
            if (m_available) {
                m_available = false;
                emit changed();
            }
            return;
        }
        const QString out = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
        // Format: device,class,brightness,max,percent
        const QStringList parts = out.split(QLatin1Char(','));
        if (parts.size() >= 5) {
            QString percentStr = parts.last();
            percentStr.remove(QLatin1Char('%'));
            const int percent = percentStr.toInt();
            const qreal b = percent / 100.0;
            if (!m_available || !qFuzzyCompare(b, m_brightness)) {
                m_available = true;
                m_brightness = b;
                emit changed();
            }
        }
    });
    p->start(m_brightnessctl, {QStringLiteral("-m"), QStringLiteral("info")});
}

void BrightnessControl::scheduleRefresh()
{
    m_refreshDebounce.start();
}

void BrightnessControl::setBrightness(qreal brightness)
{
    if (m_brightnessctl.isEmpty())
        return;
    brightness = qBound(MinBrightness, brightness, 1.0);
    QProcess::startDetached(m_brightnessctl,
                            {QStringLiteral("set"),
                             QString::number(qRound(brightness * 100)) + QStringLiteral("%")});
    m_brightness = brightness;
    m_restoreBrightness = brightness;
    persist();
    emit changed();
}

void BrightnessControl::persist()
{
    QSettings s(DockConfig::settingsFilePath(), QSettings::IniFormat);
    s.setValue(kBrightnessKey, m_brightness);
}

void BrightnessControl::onPrepareForSleep(bool sleeping)
{
    if (sleeping) {
        // Snapshot the brightness at the moment of suspend so we can restore it.
        m_restoreBrightness = m_brightness;
        persist();
    } else if (m_restoreBrightness >= MinBrightness) {
        setBrightness(m_restoreBrightness);
    }
}
