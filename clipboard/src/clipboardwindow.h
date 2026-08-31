// A normal, independently running clipboard history window.

#pragma once

#include <QQuickView>
#include <QString>

class ClipboardHistory;

class ClipboardWindow : public QQuickView
{
    Q_OBJECT
    Q_PROPERTY(bool alwaysOnTop READ alwaysOnTop WRITE setAlwaysOnTop NOTIFY alwaysOnTopChanged)
    Q_PROPERTY(bool closeAfterCopy READ closeAfterCopy WRITE setCloseAfterCopy NOTIFY closeAfterCopyChanged)
public:
    ClipboardWindow(ClipboardHistory *history, QWindow *parent = nullptr);

    bool alwaysOnTop() const { return m_alwaysOnTop; }
    bool closeAfterCopy() const { return m_closeAfterCopy; }

    Q_INVOKABLE void setAlwaysOnTop(bool on);
    Q_INVOKABLE void setCloseAfterCopy(bool on);
    Q_INVOKABLE void hideWindow();
    Q_INVOKABLE void closeWindow();

    // Select the requested monitor, then let the compositor place this normal
    // resizable toplevel as it did before anchor hints existed.
    void showWindow(const QString &screenName = QString());

signals:
    void alwaysOnTopChanged();
    void closeAfterCopyChanged();

protected:
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    static QString settingsPath();
    void saveGeometrySettings();

    ClipboardHistory *m_history;
    bool m_alwaysOnTop = false;
    bool m_closeAfterCopy = false;
};
