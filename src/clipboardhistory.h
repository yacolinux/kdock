// System clipboard history (text and images), exposed to QML as
// "clipboardHistory". Keeps up to kMaxEntries most-recent unique entries and
// persists them under the XDG data dir so the history survives full shutdowns:
// an ordered index in ~/.local/share/kdock/clipboard-history.json plus one PNG
// per image entry in ~/.local/share/kdock/clipboard-images/.
// ~/.local/share/kdock/clipboard-history.txt is still written on every save as
// a human-readable export (it is what "Ver historial actual" opens).
// A single shared instance is used by every dock.
//
// Capture backend: on Wayland the history is fed by WaylandClipboard
// (ext-data-control), which works while the dock has no keyboard focus. Where
// that protocol is missing (X11, Xvfb) it falls back to QClipboard, which only
// sees changes while the dock holds focus.

#pragma once

#include <QList>
#include <QObject>
#include <QSize>
#include <QString>
#include <QVariantList>

class WaylandClipboard;
class QThread;

class ClipboardHistory : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool captureImages READ captureImages WRITE setCaptureImages NOTIFY captureImagesChanged)
public:
    explicit ClipboardHistory(QObject *parent = nullptr);
    ~ClipboardHistory() override;

    static constexpr int kMaxEntries = 50;
    // Images are orders of magnitude bigger than text, so they get their own,
    // tighter cap on top of kMaxEntries.
    static constexpr int kMaxImages = 20;
    static constexpr qsizetype kMaxImageBytes = 8 * 1024 * 1024;

    // One clipboard entry: text or image, never both.
    struct Entry {
        QString text;      // text entry
        QString imageFile; // file name (not path) inside the images dir
        QSize imageSize;
        bool isImage() const { return !imageFile.isEmpty(); }
    };

    int count() const { return int(m_entries.size()); }

    bool captureImages() const { return m_captureImages; }
    void setCaptureImages(bool on);

    // Most-recent first. Each element is
    //   { text, preview, isImage, file, imageUrl, width, height }
    // where preview is a single-line excerpt (text) or a size label (image).
    // Filtered (case insensitive substring) by query when non-empty; a non-empty
    // query hides image entries, which have nothing to match against.
    Q_INVOKABLE QVariantList entries(const QString &query = QString()) const;

    // Put an existing history entry back on the system clipboard (and move it
    // to the top). Used when the user clicks a row in the popup.
    Q_INVOKABLE void setClipboard(const QString &text);
    Q_INVOKABLE void setClipboardImage(const QString &fileName);

    // Empty the history in memory and on disk (images included).
    Q_INVOKABLE void clearHistory();

    // Open the plain-text export of the history in the default text editor.
    Q_INVOKABLE void openInEditor();

    // Ask the user (QFileDialog) for a path and export the current history to
    // a plain-text file there.
    Q_INVOKABLE void saveHistoryDialog();

    // Absolute path of the plain-text export.
    static QString historyFilePath();
    // Absolute path of the JSON index that is the actual storage.
    static QString indexFilePath();
    // Directory holding the PNGs of the image entries (created on demand).
    static QString imagesDirPath();

signals:
    void changed();
    void captureImagesChanged();

private:
    void captureClipboard(); // QClipboard fallback path
    void pushText(const QString &text);
    // Stores the bytes as a PNG and pushes an image entry. Returns false when
    // the data is not a readable image or is over the size cap.
    bool pushImage(const QByteArray &data);
    void pushEntry(const Entry &entry); // dedup + cap; does not save
    void trim();                        // enforce kMaxEntries / kMaxImages
    void load();
    void save() const;
    void writeIndex() const;
    void writeTextExport(const QString &path) const;
    void saveAsync();
    // Deletes PNGs no entry refers to (evicted entries, leftovers from a crash).
    void sweepOrphanImages() const;
    static QString previewOf(const Entry &entry);

    QList<Entry> m_entries; // most-recent first
    // Text we just pushed onto the clipboard ourselves, to skip re-capturing
    // the resulting dataChanged() as a "new" copy (QClipboard path only; the
    // Wayland backend knows it owns the selection).
    QString m_ownText;
    bool m_captureImages = true;
    WaylandClipboard *m_wayland = nullptr;
    QThread *m_saveThread = nullptr;
    bool m_saveAgain = false;
};
