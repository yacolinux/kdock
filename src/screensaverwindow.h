#pragma once

#include <QWebEngineView>

class QScreen;
class QResizeEvent;
class QToolButton;
class VirtualDesktops;

class ScreensaverWindow : public QWebEngineView
{
    Q_OBJECT
public:
    explicit ScreensaverWindow(const QString &screenName, VirtualDesktops *desktops,
                               QWidget *parent = nullptr, int monitorIndex = 0);
    ~ScreensaverWindow() override;

    QString screenName() const { return m_screenName; }
    // engine < 0 uses the configured engine. A non-empty page selects one
    // After Dark page for a manual activation without changing settings.
    void showSaver(int engine = -1, const QString &afterDarkPage = QString());
    void hideSaver();
    void updateScreen();
    void refreshConfig();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void userDismissed();

private:
    QString wallpaperHtml() const;
    QString afterDarkHtml(const QString &page = QString()) const;
    void prepareWallpaper();
    void prepareAfterDark(const QString &page = QString());
    void advanceContent();
    QString currentWallpaperFolder() const;
    int currentWallpaperDesktop() const;
    void applyLayerProperties();

    void resizeEvent(QResizeEvent *event) override;

    QString m_screenName;
    VirtualDesktops *m_desktops = nullptr;
    quintptr m_boundOutput = 0;
    int m_monitorIndex = 0;
    int m_activeEngine = -1;
    QStringList m_wallpaperFiles;
    int m_wallpaperIndex = 0;
    QString m_afterDarkPage;
    int m_afterDarkIndex = 0;
    QToolButton *m_changeButton = nullptr;
};
