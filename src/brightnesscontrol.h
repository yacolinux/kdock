// Brightness of the *one* monitor the dock widget drives: PowerDevil's
// org.kde.ScreenBrightness when it is there (the only thing that knows about
// external displays), the internal backlight through brightnessctl otherwise.
//
// Which monitor that is comes from Brightness/wheelDisplay in the shared config
// and is picked in the VideoEnergía tab; see wheelDisplay(). The widget never
// drives more than one screen at a time, the same way the volume widget only
// ever drives the default sink — the other monitors are that tab's job.

#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class ScreenBrightness;

class BrightnessControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(qreal brightness READ brightness NOTIFY changed)
    // Label of the monitor the wheel drives, for the widget's tooltip. Empty
    // when that is the internal backlight (there is nothing better to call it).
    Q_PROPERTY(QString targetLabel READ targetLabel NOTIFY changed)
    // The brightnessctl backlight on its own — properties and not just getters
    // because the control panel's Video card draws a row from them.
    Q_PROPERTY(bool internalAvailable READ internalAvailable NOTIFY changed)
    Q_PROPERTY(qreal internalBrightness READ internalBrightness NOTIFY changed)

public:
    // Never let the screen go fully dark; keep at least this fraction of brightness.
    static constexpr qreal MinBrightness = 0.05;

    // Value of Brightness/wheelDisplay that pins the wheel to the internal
    // backlight (brightnessctl) even when PowerDevil offers displays. Empty
    // means "auto"; anything else is a monitor label.
    static const QString InternalTarget;

    explicit BrightnessControl(QObject *parent = nullptr);

    // Injected from main.cpp: this object is constructed first and PowerDevil
    // may answer later, so nothing here may cache the display list.
    void setScreens(ScreenBrightness *screens);
    ScreenBrightness *screens() const { return m_screens; }

    bool available() const;
    qreal brightness() const;
    QString targetLabel() const;

    // The monitor the wheel drives, resolved fresh on every call:
    //   empty key   -> auto: PowerDevil's internal display, else the
    //                  brightnessctl backlight (PowerDevil normally does not
    //                  list the laptop panel at all), else the first display,
    //   "internal"  -> the brightnessctl backlight,
    //   any label   -> that monitor, falling back to auto when it is gone.
    // Returns the PowerDevil *object name*, or an empty string when the target
    // is the brightnessctl backlight. Never store the result: PowerDevil
    // renumbers those names when a monitor sleeps (see screenbrightness.h).
    QString wheelDisplay() const;

    // Persisted preference behind wheelDisplay(): "" (auto), "internal", or a
    // monitor label. Labels and not object names, for the same reason.
    Q_INVOKABLE QString wheelTarget() const { return m_wheelTarget; }
    Q_INVOKABLE void setWheelTarget(const QString &target);

    // The brightnessctl backlight on its own, whatever the wheel is pointed at.
    // The VideoEnergía tab gives it a row of its own when PowerDevil doesn't
    // report an internal display (which is the usual case with a dock station:
    // PowerDevil lists the DDC monitors only).
    bool internalAvailable() const { return m_available; }
    qreal internalBrightness() const { return m_brightness; }
    Q_INVOKABLE void setInternalBrightness(qreal brightness);

    Q_INVOKABLE void setBrightness(qreal brightness);
    Q_INVOKABLE void increase() { setBrightness(brightness() + 0.05); }
    Q_INVOKABLE void decrease() { setBrightness(brightness() - 0.05); }

signals:
    void changed();

private slots:
    // logind org.freedesktop.login1.Manager.PrepareForSleep(bool)
    void onPrepareForSleep(bool sleeping);

private:
    void refresh();
    void scheduleRefresh();
    void persist();

    ScreenBrightness *m_screens = nullptr;
    QString m_wheelTarget;
    QString m_brightnessctl;
    bool m_available = false;
    qreal m_brightness = 1.0;
    // Brightness to re-apply on resume/startup (firmware often resets it).
    qreal m_restoreBrightness = -1.0;
    QTimer m_refreshDebounce;
};
