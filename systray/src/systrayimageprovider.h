// Exposes SNI items' raw IconPixmap to QML as image://systray/<service>.
// Used as a fallback for tray items that provide no themed IconName.

#pragma once

#include <QQuickImageProvider>

class SystrayHost;

class SystrayImageProvider : public QQuickImageProvider
{
public:
    explicit SystrayImageProvider(SystrayHost *host)
        : QQuickImageProvider(QQuickImageProvider::Pixmap), m_host(host) {}
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    SystrayHost *m_host;
};
