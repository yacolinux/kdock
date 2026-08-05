// Static routes editor for one IP family, opened from the IPv4/IPv6 pages of
// the connection editor. Mirrors the same dialog in KDE's network KCM: a table
// of destination/prefix/next hop/metric plus the two switches that decide what
// NM does with the routes it gets automatically.

#pragma once

#include "networksettings.h"

#include <QDialog>

class QCheckBox;
class QTableWidget;

class RoutesDialog : public QDialog
{
    Q_OBJECT
public:
    RoutesDialog(const NetworkSettings::IpConfig &config, bool v6, QWidget *parent = nullptr);

    QList<NetworkSettings::IpRoute> routes() const;
    bool ignoreAutoRoutes() const;
    bool neverDefault() const;

private:
    void addRow(const NetworkSettings::IpRoute &route);

    QTableWidget *m_table;
    QCheckBox *m_ignoreAuto;
    QCheckBox *m_neverDefault;
    bool m_v6;
};
