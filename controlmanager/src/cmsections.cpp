#include "cmsections.h"

#include <QCoreApplication>
#include <QVariantMap>

namespace {

// Icon names picked against the rendered panel, not against a `find` in
// /usr/share/icons: several plausible names exist on disk and draw as something
// unrecognizable in the iconset the dock actually uses.
const QList<CmSectionInfo> &table()
{
    static const QList<CmSectionInfo> t = {
        // id            icon                              w  h  minW minH tab
        // Born small (2 cells wide, 3 fit a 6-column row) since 2026-08-09:
        // the old 3x2/4x2 defaults filled a row with two cards and the grid
        // read as "only 2 columns". Existing layouts keep their sizes until
        // the user resets the arrangement.
        {QStringLiteral("clock"),     QStringLiteral("clock"),                          2, 2, 2, 1, false},
        {QStringLiteral("audio"),     QStringLiteral("audio-volume-high"),              2, 2, 2, 1, true},
        {QStringLiteral("video"),     QStringLiteral("preferences-system-power-management"), 2, 2, 2, 1, true},
        {QStringLiteral("calendar"),  QStringLiteral("office-calendar"),                2, 2, 2, 2, true},
        {QStringLiteral("play"),      QStringLiteral("applications-multimedia"),        2, 2, 2, 1, true},
        {QStringLiteral("network"),   QStringLiteral("network-wireless"),               2, 2, 2, 1, true},
        {QStringLiteral("wallpaper"), QStringLiteral("preferences-desktop-wallpaper"),  1, 1, 1, 1, true},
        {QStringLiteral("system"),    QStringLiteral("preferences-system"),             2, 2, 1, 1, true},
        {QStringLiteral("appearance"),QStringLiteral("preferences-desktop-theme"),       2, 2, 2, 1, true},
        {QStringLiteral("desktops"),  QStringLiteral("virtual-desktops"),                2, 1, 1, 1, true},
    };
    return t;
}

} // namespace

const QList<CmSectionInfo> &CmSections::all()
{
    return table();
}

CmSectionInfo CmSections::byId(const QString &id)
{
    for (const CmSectionInfo &s : table()) {
        if (s.id == id)
            return s;
    }
    return {};
}

bool CmSections::exists(const QString &id)
{
    return !byId(id).id.isEmpty();
}

QStringList CmSections::ids()
{
    QStringList out;
    for (const CmSectionInfo &s : table())
        out.append(s.id);
    return out;
}

QStringList CmSections::tabIds()
{
    QStringList out;
    for (const CmSectionInfo &s : table()) {
        if (s.hasTab)
            out.append(s.id);
    }
    return out;
}

QString CmSections::label(const QString &id)
{
    if (id == QLatin1String("clock"))
        return QCoreApplication::translate("CmSections", "Reloj");
    if (id == QLatin1String("audio"))
        return QCoreApplication::translate("CmSections", "Audio");
    if (id == QLatin1String("video"))
        return QCoreApplication::translate("CmSections", "Video y energía");
    if (id == QLatin1String("calendar"))
        return QCoreApplication::translate("CmSections", "Calendario");
    if (id == QLatin1String("play"))
        return QCoreApplication::translate("CmSections", "Reproducción");
    if (id == QLatin1String("network"))
        return QCoreApplication::translate("CmSections", "Red");
    if (id == QLatin1String("wallpaper"))
        return QCoreApplication::translate("CmSections", "Fondo de escritorio");
    if (id == QLatin1String("system"))
        return QCoreApplication::translate("CmSections", "Sistema");
    if (id == QLatin1String("appearance"))
        return QCoreApplication::translate("CmSections", "Apariencia");
    if (id == QLatin1String("desktops"))
        return QCoreApplication::translate("CmSections", "Escritorios");
    return id;
}

QString CmSections::description(const QString &id)
{
    if (id == QLatin1String("clock"))
        return QCoreApplication::translate("CmSections", "Hora y fecha en grande.");
    if (id == QLatin1String("audio"))
        return QCoreApplication::translate("CmSections",
                                           "Mezclador: salidas, entradas y volumen por aplicación.");
    if (id == QLatin1String("video"))
        return QCoreApplication::translate("CmSections",
                                           "Brillo de cada monitor, perfil de energía y modo oscuro.");
    if (id == QLatin1String("calendar"))
        return QCoreApplication::translate("CmSections", "Calendario de mes.");
    if (id == QLatin1String("play"))
        return QCoreApplication::translate("CmSections",
                                           "Lo que se está reproduciendo, con sus controles.");
    if (id == QLatin1String("network"))
        return QCoreApplication::translate("CmSections",
                                           "Conexiones guardadas y redes Wi-Fi cercanas.");
    if (id == QLatin1String("wallpaper"))
        return QCoreApplication::translate("CmSections",
                                           "Avanzar el fondo de un monitor o de todos.");
    if (id == QLatin1String("system"))
        return QCoreApplication::translate("CmSections",
                                           "Las cuatro configuraciones, reinicios y sesión.");
    if (id == QLatin1String("appearance"))
        return QCoreApplication::translate("CmSections",
                                           "Iconset y esquema de color de todo el escritorio.");
    if (id == QLatin1String("desktops"))
        return QCoreApplication::translate("CmSections",
                                           "Cambiar de escritorio virtual.");
    return QString();
}

QVariantList CmSections::toVariantList()
{
    QVariantList out;
    for (const CmSectionInfo &s : table()) {
        QVariantMap m;
        m[QStringLiteral("id")] = s.id;
        m[QStringLiteral("label")] = label(s.id);
        m[QStringLiteral("description")] = description(s.id);
        m[QStringLiteral("icon")] = s.icon;
        m[QStringLiteral("hasTab")] = s.hasTab;
        m[QStringLiteral("defaultW")] = s.defaultW;
        m[QStringLiteral("defaultH")] = s.defaultH;
        out.append(m);
    }
    return out;
}
