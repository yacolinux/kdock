// Exposes XDG themed icons to QML as image://icon/<name>[@rev[@theme]]
//
// The optional third field names an icon theme to resolve this one icon
// against, instead of the globally selected one (see Theme). Widget icons use
// it to pull a light- or dark-background variant of the standard icon set, so
// monochrome icons stay legible over a custom panel color.

#pragma once

#include <QQuickImageProvider>

class IconProvider : public QQuickImageProvider
{
public:
    IconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
};
