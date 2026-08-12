#include "brightnesscontrol.h"

#include "dockconfig.h"
#include "screenbrightness.h"

#include <QDBusConnection>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

static const QString kBrightnessKey = QStringLiteral("Brightness/lastBrightness");
static const QString kWheelKey = QStringLiteral("Brightness/wheelDisplay");

const QString BrightnessControl::InternalTarget = QStringLiteral("internal");

BrightnessControl::BrightnessControl(QObject *parent)
    : QObject(parent)
{
    {
        QSettings s(DockConfig::settingsFilePath(), QSettings::IniFormat);
        m_wheelTarget = s.value(kWheelKey).toString();
    }

    m_brightnessctl = QStandardPaths::findExecutable(QStringLiteral("brightnessctl"));
    if (m_brightnessctl.isEmpty()) {
        // Not fatal any more: with PowerDevil around the widget still works,
        // it just cannot reach the internal backlight.
        qInfo("kdock: brightnessctl not found, only PowerDevil displays are available");
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
    // have, e.g. after a hard poweroff). Internal panel only, always: this
    // value was saved from that backlight, and pushing it into an external
    // monitor over DDC at every login would be a nasty surprise.
    if (m_restoreBrightness >= MinBrightness)
        setInternalBrightness(m_restoreBrightness);
}

void BrightnessControl::setScreens(ScreenBrightness *screens)
{
    if (m_screens == screens)
        return;
    if (m_screens)
        disconnect(m_screens, nullptr, this, nullptr);
    m_screens = screens;
    if (m_screens) {
        // KDE (or the monitor's own buttons, through DDC) can move the
        // brightness behind our back; the widget's bar has to follow.
        connect(m_screens, &ScreenBrightness::changed, this, &BrightnessControl::changed);
    }
    emit changed();
}

QString BrightnessControl::wheelDisplay() const
{
    if (!m_screens || m_wheelTarget == InternalTarget)
        return QString();
    const QVariantList displays = m_screens->displays();
    if (displays.isEmpty())
        return QString();

    if (!m_wheelTarget.isEmpty()) {
        for (const QVariant &v : displays) {
            const QVariantMap d = v.toMap();
            if (d.value(QStringLiteral("label")).toString() == m_wheelTarget)
                return d.value(QStringLiteral("name")).toString();
        }
        // The chosen monitor is unplugged (or PowerDevil renamed it): fall
        // through to auto rather than leaving the wheel dead.
    }

    for (const QVariant &v : displays) {
        const QVariantMap d = v.toMap();
        if (d.value(QStringLiteral("internal")).toBool())
            return d.value(QStringLiteral("name")).toString();
    }
    return displays.first().toMap().value(QStringLiteral("name")).toString();
}

void BrightnessControl::setWheelTarget(const QString &target)
{
    if (m_wheelTarget == target)
        return;
    m_wheelTarget = target;
    QSettings s(DockConfig::settingsFilePath(), QSettings::IniFormat);
    s.setValue(kWheelKey, m_wheelTarget);
    emit changed();
}

bool BrightnessControl::available() const
{
    return m_available || !wheelDisplay().isEmpty();
}

qreal BrightnessControl::brightness() const
{
    const QString name = wheelDisplay();
    if (name.isEmpty())
        return m_brightness;
    const QVariantList displays = m_screens->displays();
    for (const QVariant &v : displays) {
        const QVariantMap d = v.toMap();
        if (d.value(QStringLiteral("name")).toString() == name)
            return d.value(QStringLiteral("value")).toReal();
    }
    return m_brightness;
}

QString BrightnessControl::targetLabel() const
{
    const QString name = wheelDisplay();
    if (name.isEmpty())
        return QString();
    const QVariantList displays = m_screens->displays();
    for (const QVariant &v : displays) {
        const QVariantMap d = v.toMap();
        if (d.value(QStringLiteral("name")).toString() == name)
            return d.value(QStringLiteral("label")).toString();
    }
    return QString();
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
        // Format: device,class,current,percent,max — the percentage is the
        // *fourth* field and the last one is the raw maximum. Reading the last
        // one instead (which is what this did until 2026-08-12) left
        // m_brightness at 960 on a panel whose max is 96000: the bar was
        // pinned full and, worse, decrease() computed 960 - 0.05 and clamped
        // back to 1.0, so the wheel down did *nothing*. Picked by the '%' and
        // not by index, so a format change is a no-op instead of a wrong value.
        const QStringList parts = out.split(QLatin1Char(','));
        int percentField = -1;
        for (int i = 0; i < parts.size(); ++i) {
            if (parts.at(i).endsWith(QLatin1Char('%')))
                percentField = i;
        }
        if (parts.size() >= 5 && percentField >= 0) {
            QString percentStr = parts.at(percentField);
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
    // One monitor and only one: the wheel never touches the others (that is
    // what the VideoEnergía tab is for).
    const QString name = wheelDisplay();
    if (!name.isEmpty()) {
        m_screens->setBrightness(name, brightness);
        // ScreenBrightness updates optimistically and emits changed(), which we
        // relay; nothing to persist here — this backlight is PowerDevil's.
        return;
    }
    setInternalBrightness(brightness);
}

void BrightnessControl::setInternalBrightness(qreal brightness)
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
        // Same as at startup: the snapshot is the internal backlight's.
        setInternalBrightness(m_restoreBrightness);
    }
}
