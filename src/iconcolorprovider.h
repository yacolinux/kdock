// Computes a representative "dominant" color for a themed icon, exposed to
// QML as the "iconColors" context property. Used to tint the running-app
// background behind dock icons. Results are cached per icon name and
// invalidated when the theme revision changes.

#pragma once

#include <QColor>
#include <QHash>
#include <QObject>

class IconColorProvider : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    // revision (theme.revision) busts the cache and forces QML bindings that
    // call this to re-evaluate on theme changes.
    Q_INVOKABLE QColor dominant(const QString &iconName, int revision = 0);

    // A color that stands out against a fill of dominant() — the RGB inversion
    // of the dominant, with a black/white fallback for near-gray icons. Used to
    // keep the dots/edge-line visible when drawn over the colored running-app
    // background (which is the dominant color).
    Q_INVOKABLE QColor contrasting(const QString &iconName, int revision = 0);

private:
    QColor compute(const QString &iconName) const;

    QHash<QString, QColor> m_cache;
    int m_revision = -1;
};
