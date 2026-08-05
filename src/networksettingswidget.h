// The Settings → Redes tab: devices and saved connections on the left, a
// read-only device page or the connection editor on the right. Modeled on
// KDE's network KCM. Reached from the network widget's right-click
// (DockWindow::openNetworkSettings) the same way the mixer is.

#pragma once

#include "networksettings.h"

#include <QWidget>

class ConnectionEditor;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

class NetworkSettingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit NetworkSettingsWidget(QWidget *parent = nullptr);

private:
    void reload();
    void selectionChanged();
    void showDevice(const NetworkSettings::Device &device);
    void showConnection(const QString &connPath);
    void addConnection(const QString &type);
    void deleteSelected();
    void toggleSelected();
    void applyEditor();
    void updateButtons();
    // Path of the connection the tree has selected, empty when it is a device.
    QString selectedConnPath() const;

    NetworkSettings m_backend;
    QList<NetworkSettings::Device> m_devices;
    QList<NetworkSettings::Connection> m_connections;

    QTreeWidget *m_tree;
    QStackedWidget *m_stack;
    QLabel *m_devicePage;
    ConnectionEditor *m_editor;
    QLabel *m_placeholder;
    QPushButton *m_addButton;
    QPushButton *m_deleteButton;
    QPushButton *m_toggleButton;
    QLabel *m_status;

    // A reload rebuilds the tree, so it is deferred while the editor has
    // unsaved changes and coalesced (NM emits a burst of PropertiesChanged
    // during any activation).
    QTimer *m_reloadTimer;
    // Path of the connection being edited, empty when it has not been saved yet.
    QString m_editingPath;
    bool m_editingNew = false;
};
