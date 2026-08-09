// Registering a global shortcut of our own with KDE's kglobalaccel, over raw
// D-Bus (no KDE Frameworks linkage, same rule as every other backend here).
//
// This is the other half of KWinShortcut, which only *fires* shortcuts that
// already exist. Here kdock publishes an action of its own so it shows up in
// Preferencias del sistema → Atajos, where the user assigns a key to it.
//
// Deliberately registered with **no** default key combination: stealing a combo
// from whatever the user already has bound is worse than making them pick one.
// The action is visible in the KCM either way, and it can also be fired from a
// script with `qdbus6 org.kde.kglobalaccel /component/kdock invokeShortcut <id>`.

#pragma once

#include <QObject>
#include <QString>

class GlobalShortcuts : public QObject
{
    Q_OBJECT
public:
    explicit GlobalShortcuts(QObject *parent = nullptr);

    // Publishes <actionId> under the "kdock" component. Returns false when
    // kglobalaccel is not on the bus (a non-KDE session), in which case nothing
    // else here does anything either.
    bool registerAction(const QString &actionId, const QString &friendlyName);

signals:
    // The user pressed the key they assigned, or someone called invokeShortcut.
    void triggered(const QString &actionId);

private slots:
    void onPressed(const QString &component, const QString &action, qlonglong timestamp);

private:
    bool m_connected = false;
};
