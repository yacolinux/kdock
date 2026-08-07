// User-editable translation layer.
//
// The strings written in the code are the native layer ("capabase"); a
// translation is a plain .md file in ~/.local/share/kdock/translations that
// replaces them. Configuracion/UIdock are keyed by the capabase string itself
// and are served through a QTranslator, so every existing tr()/qsTr() call goes
// through this layer without touching a single call site. Widgets are keyed by
// section token and Apps by .desktop id, which the dock asks for directly.
//
// Anything a file does not cover falls back on its own: capabase for the first
// three sections, the .desktop Name= for apps.

#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTranslator>

struct DesktopEntry;
class QFileSystemWatcher;

// Serves the Configuracion + UIdock entries to tr()/qsTr(). The two sections
// share one map on purpose: the split is there to make the file readable, and
// keying the lookup on Qt's context would break a translation every time a
// string moved between classes.
class KdockTranslator : public QTranslator
{
    Q_OBJECT
public:
    using QTranslator::QTranslator;

    QString translate(const char *context, const char *sourceText,
                      const char *disambiguation = nullptr, int n = -1) const override;
    bool isEmpty() const override { return false; }

    void setTable(QHash<QString, QString> table) { m_table = std::move(table); }

private:
    QHash<QString, QString> m_table;
};

class Translations : public QObject
{
    Q_OBJECT
public:
    explicit Translations(QObject *parent = nullptr);
    ~Translations() override;

    // The layer is asked for from deep inside DockConfig/DockModel, which are
    // also built by the test probes: a null instance simply means capabase.
    static Translations *instance();

    static QString dirPath();
    static QString filePathFor(const QString &name);

    // Names (file base names) of the .md files on disk, capabase first.
    QStringList available() const;
    QString activeName() const { return m_active; }
    void setActive(const QString &name);
    void reload();

    // Empty when the active file has no entry for it (the caller keeps its own
    // default: the capabase label, or the .desktop Name=).
    QString widget(const QString &token) const;
    QString app(const QString &desktopId) const;

    // Appends "<id> = <Name>" to the Apps section of `name` for every entry it
    // does not have yet; returns how many were added.
    int refreshApps(const QString &name, const QList<DesktopEntry> &entries);
    // Duplicates `baseName` under `newName`; false (with *error set) on failure.
    bool createFrom(const QString &baseName, const QString &newName, QString *error);

signals:
    // The active file changed, or was edited on disk: every visible string has
    // to be asked for again.
    void changed();

private:
    void seedFiles();
    void loadActive();

    KdockTranslator *m_translator = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    QString m_active;
    QHash<QString, QString> m_widgets;
    QHash<QString, QString> m_apps;
};

// The one place that turns a .desktop entry into the name the user sees:
// the Apps section of the active layer, or the entry's own Name= .
QString appNameFor(const DesktopEntry &entry);
