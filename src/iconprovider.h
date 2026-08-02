// Exposes XDG themed icons to QML as image://icon/<name>[@rev[@theme]]
//
// The optional third field names an icon theme to resolve this one icon
// against, instead of the globally selected one (see Theme). Widget icons use
// it to pull a light- or dark-background variant of the standard icon set, so
// monochrome icons stay legible over a custom panel color.
//
// That third field is resolved by *reading the theme directory*, never by
// touching QIcon::setThemeName(): the icon theme is process-global state, and
// swapping it for the duration of one lookup leaks. QIcon::setThemeName()
// notifies the item views synchronously, which re-enters this provider from
// inside the swap (verified 2026-08-02); an app icon that lands in that window
// is drawn with the widget icon set and QML then caches that pixmap under a URL
// that carries no theme, so it stays wrong until theme.revision changes.

#pragma once

#include <QHash>
#include <QQuickImageProvider>
#include <QString>

class IconProvider : public QQuickImageProvider
{
public:
    IconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;

    // Absolute path of <name> inside <themeId> (following its Inherits chain),
    // for an icon of about <size> px; empty when no theme in the chain has it.
    // Static so a probe can exercise it without a QML engine.
    static QString resolveInTheme(const QString &themeId, const QString &name, int size);

private:
    // Resolved paths, keyed "theme|name|size". Icon themes do not change on
    // disk while the dock runs; the revision carried by the URL clears it
    // anyway, same as IconColorProvider.
    QHash<QString, QString> m_paths;
    int m_revision = -1;
};
