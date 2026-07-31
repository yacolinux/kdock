#include "iconprovider.h"

#include <QIcon>

QPixmap IconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    // "name@rev[@theme]": the revision busts the cache on theme changes, the
    // optional theme resolves this icon against another icon set (see header).
    const QString name = id.section(QLatin1Char('@'), 0, 0);
    const QString themeOverride = id.section(QLatin1Char('@'), 2, 2);

    const QSize wanted = requestedSize.isValid() ? requestedSize : QSize(64, 64);

    if (!themeOverride.isEmpty() && themeOverride != QIcon::themeName()) {
        // Qt resolves themed icons against one global theme name, so swap it
        // for the lookup and put it back. The pixmap has to be rendered while
        // the override is active: QIcon resolves lazily, and re-checks the
        // current theme every time it is asked for a pixmap.
        const QString previous = QIcon::themeName();
        QIcon::setThemeName(themeOverride);
        // hasThemeIcon() keeps names the override doesn't carry (custom app
        // icons, or a theme that isn't installed) on the normal path below
        // instead of turning them into a missing-icon placeholder.
        const QPixmap pm = QIcon::hasThemeIcon(name) ? QIcon::fromTheme(name).pixmap(wanted)
                                                     : QPixmap();
        QIcon::setThemeName(previous);
        if (!pm.isNull()) {
            if (size)
                *size = pm.size();
            return pm;
        }
    }

    QIcon icon = QIcon::fromTheme(name);
    if (icon.isNull() && name.contains(QLatin1Char('/')))
        icon = QIcon(name); // absolute path in the Icon= field
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));

    QPixmap pm = icon.pixmap(wanted);
    if (size)
        *size = pm.size();
    return pm;
}
