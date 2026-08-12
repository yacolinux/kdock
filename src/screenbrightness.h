// Per-monitor brightness through PowerDevil's org.kde.ScreenBrightness (session
// bus). No KDE Frameworks linkage: raw Qt6::DBus, like every other backend here.
//
// This is what BrightnessControl cannot do: `brightnessctl` drives the *internal*
// backlight and nothing else, while PowerDevil exposes one object per display
// (DDC/CI for the external ones) with a label, a range and a setter.
//
// The display object names are volatile — they are renumbered when a monitor
// sleeps and comes back (observed live: display11/display12 became display13
// between two runs minutes apart). So nothing may cache them: DisplayAdded /
// DisplayRemoved rescan, and every call resolves the path again.

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class ScreenBrightness : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    // Never let a screen go fully dark; keep at least this fraction. Same floor
    // BrightnessControl uses for the internal panel.
    static constexpr qreal MinBrightness = 0.05;

    explicit ScreenBrightness(QObject *parent = nullptr);

    bool available() const { return m_available; }
    int count() const { return m_displays.size(); }

    // One entry per display, in the order PowerDevil reports them. Keys:
    //   name (the D-Bus object name), label (the monitor's own name),
    //   internal (bool), value (0..1), brightness (raw), max (raw).
    Q_INVOKABLE QVariantList displays() const { return m_displays; }

    // ratio is 0..1 and is clamped to MinBrightness.
    Q_INVOKABLE void setBrightness(const QString &name, qreal ratio);
    Q_INVOKABLE void setAll(qreal ratio);

    // Test seam, off unless the variable is set: KDOCK_TEST_DISPLAYS holds a
    // JSON array of display objects ([{"name":…,"label":…,"internal":…,
    // "brightness":…,"max":…}]) and replaces the D-Bus scan, so the monitor
    // logic can be exercised without PowerDevil — and without dimming anybody's
    // screen. Setters still go out on D-Bus, where the invented object names
    // resolve to nothing.
    static QVariantList fixtureDisplays();

public slots:
    // A slot, not just Q_INVOKABLE: QDBusConnection::connect() resolves the
    // SLOT() macro through indexOfSlot(), which does not see plain invokables.
    void refresh();

signals:
    void changed();

private slots:
    void onBrightnessChanged(const QString &name, int brightness);
    void onRangeChanged(const QString &name, int maxBrightness);

private:
    void connectSignals();
    int indexOf(const QString &name) const;

    bool m_available = false;
    QVariantList m_displays;
};
