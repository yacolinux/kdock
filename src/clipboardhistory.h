// System clipboard history (text only), exposed to QML as "clipboardHistory".
// Watches QClipboard for text changes, keeps up to kMaxEntries most-recent
// unique entries, and persists them to a plain-text file under the XDG data
// dir (~/.local/share/kdock/clipboard-history.txt) so the history survives
// full shutdowns. A single shared instance is used by every dock.

#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

class ClipboardHistory : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
public:
    explicit ClipboardHistory(QObject *parent = nullptr);

    static constexpr int kMaxEntries = 50;

    int count() const { return m_entries.size(); }

    // Most-recent first. Each element is { text, preview } where preview is a
    // single-line, trimmed excerpt for the list delegate. Filtered (case
    // insensitive substring) by query when non-empty.
    Q_INVOKABLE QVariantList entries(const QString &query = QString()) const;

    // Put an existing history entry back on the system clipboard (and move it
    // to the top). Used when the user clicks a row in the popup.
    Q_INVOKABLE void setClipboard(const QString &text);

    // Empty the history in memory and on disk.
    Q_INVOKABLE void clearHistory();

    // Open the persistent plain-text history file in the default text editor.
    Q_INVOKABLE void openInEditor();

    // Ask the user (QFileDialog) for a path and export the current history to
    // a plain-text file there.
    Q_INVOKABLE void saveHistoryDialog();

    // Absolute path of the persistent plain-text history file.
    static QString historyFilePath();

signals:
    void changed();

private:
    void captureClipboard();
    void pushEntry(const QString &text); // dedup + cap; does not save
    void load();
    void save() const;
    static QString previewOf(const QString &text);
    void writeToFile(const QString &path) const;

    QStringList m_entries; // most-recent first
    // Text we just pushed onto the clipboard ourselves, to skip re-capturing
    // the resulting dataChanged() as a "new" copy.
    QString m_ownText;
};
