// The catalogue of sections: the single place that knows what ControlManager can
// show, what each one is called, which icon it wears and how big its card starts.
//
// Everything else reads this table: the tab bar, the layout engine's default
// sizes, the settings panel's section list and --dump-sections. Adding a section
// is one row here plus one file in qml/cards/.
//
// The labels go through tr() at call time (not stored in the table), so a
// language change is a re-read, not a rebuild.

#pragma once

#include <QList>
#include <QString>
#include <QVariantList>

struct CmSectionInfo
{
    QString id;
    QString icon;
    // Card size in grid cells when the section is first dropped on Principal.
    int defaultW = 2;
    int defaultH = 2;
    int minW = 1;
    int minH = 1;
    // Whether the section gets a tab of its own. The clock is card-only: a whole
    // tab showing nothing but the time would be a tab wasted.
    bool hasTab = true;
};

namespace CmSections {

// In tab order (the user can reorder them; this is the factory order).
const QList<CmSectionInfo> &all();
// A null-id CmSectionInfo when the id is unknown, so callers can test .id.
CmSectionInfo byId(const QString &id);
bool exists(const QString &id);
QStringList ids();
// Ids that can carry a tab, in factory order.
QStringList tabIds();

// Translated display name. Deliberately not part of the table: the table is
// static data and this changes with the language.
QString label(const QString &id);
// Longer line for the settings panel and the tooltips.
QString description(const QString &id);

// [{ id, label, description, icon, hasTab, defaultW, defaultH }] for QML and the
// settings panel.
QVariantList toVariantList();

} // namespace CmSections
