// A self-contained Qt Widgets icon picker (no KDE Frameworks dependency).
// Lists the icons of the current XDG icon theme (the same one the dock renders
// via QIcon::fromTheme) in a searchable grid.

#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QLineEdit;
class QListWidget;
class QTimer;

class IconPickerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit IconPickerDialog(const QString &current, QWidget *parent = nullptr);

    // The icon name the user picked (empty if cancelled / none).
    QString selectedIcon() const { return m_selected; }

    // All icon names available in the current theme (+ inherited + hicolor),
    // scanned once and cached.
    static QStringList availableIconNames();

private:
    void populate(const QString &filter);

    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QTimer *m_debounce = nullptr;
    QString m_current;
    QString m_selected;
};
