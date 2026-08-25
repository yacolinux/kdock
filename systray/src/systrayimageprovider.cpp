#include "systrayimageprovider.h"

#include "systray.h"

QPixmap SystrayImageProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    // Strip the cache-busting suffix ("<service>@<serial>"); the service is the
    // bus name, which never contains '@'.
    const QString body = id.section(QLatin1Char('@'), 0, 0);

    // Two kinds of id: the item's own icon ("<service>"), and one entry of its
    // menu ("menu:<service>:<itemId>"), whose icon arrives as a raw PNG blob in
    // the DBusMenu `icon-data` property instead of a theme icon name.
    QPixmap pm;
    if (body.startsWith(QLatin1String("menu:"))) {
        const QString rest = body.mid(5);
        const int sep = rest.lastIndexOf(QLatin1Char(':'));
        const QString service = rest.left(sep);
        const int itemId = rest.mid(sep + 1).toInt();
        if (m_host && sep > 0)
            pm = m_host->menuIconPixmapForService(service, itemId);
    } else if (m_host) {
        pm = m_host->iconPixmapForService(body);
    }
    if (pm.isNull()) {
        // The item exposes neither a themed IconName nor a usable IconPixmap
        // (e.g. an app that ships an empty/transparent tray icon). Return a
        // transparent 1x1 so QML renders nothing instead of logging a warning.
        QPixmap empty(1, 1);
        empty.fill(Qt::transparent);
        if (size)
            *size = empty.size();
        return empty;
    }

    if (requestedSize.isValid() && requestedSize != pm.size())
        pm = pm.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size)
        *size = pm.size();
    return pm;
}
