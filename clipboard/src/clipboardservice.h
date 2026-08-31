// D-Bus contract between kdock and the standalone clipboard window.

#pragma once

#include <QObject>
#include <QString>

class ClipboardWindow;

class ClipboardService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kdock.Clipboard")
public:
    static QString serviceName();
    static QString objectPath();
    static bool alreadyRunning();

    static void callToggle(const QString &screenName);

    explicit ClipboardService(ClipboardWindow *window, QObject *parent = nullptr);
    bool registerOnBus();

public slots:
    Q_SCRIPTABLE void toggle(const QString &screenName);
    Q_SCRIPTABLE void hide();
    Q_SCRIPTABLE void quit();

private:
    ClipboardWindow *m_window;
};
