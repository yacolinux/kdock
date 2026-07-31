#include "clipboardhistory.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

namespace {
// Delimiter line between entries in the plain-text file. Kept human-readable
// so "Ver historial actual" (open in editor) is legible and lightly editable.
const QString kDelimiter = QStringLiteral("=== kdock clipboard entry ===");
} // namespace

ClipboardHistory::ClipboardHistory(QObject *parent)
    : QObject(parent)
{
    load();
    if (QClipboard *cb = QGuiApplication::clipboard()) {
        connect(cb, &QClipboard::dataChanged, this, &ClipboardHistory::captureClipboard);
        // Seed with whatever is already on the clipboard at startup.
        captureClipboard();
    }
}

QString ClipboardHistory::historyFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                        + QStringLiteral("/kdock");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/clipboard-history.txt");
}

QString ClipboardHistory::previewOf(const QString &text)
{
    QString t = text.trimmed();
    const int nl = t.indexOf(QLatin1Char('\n'));
    if (nl >= 0)
        t = t.left(nl) + QStringLiteral(" …");
    if (t.size() > 120)
        t = t.left(120) + QStringLiteral("…");
    return t;
}

void ClipboardHistory::captureClipboard()
{
    QClipboard *cb = QGuiApplication::clipboard();
    if (!cb)
        return;
    const QString text = cb->text();
    if (text.isEmpty())
        return;
    // Ignore the echo from our own setClipboard().
    if (text == m_ownText)
        return;
    if (!m_entries.isEmpty() && m_entries.first() == text)
        return;
    pushEntry(text);
    save();
    emit changed();
}

void ClipboardHistory::pushEntry(const QString &text)
{
    m_entries.removeAll(text); // dedup: an existing copy moves to the top
    m_entries.prepend(text);
    while (m_entries.size() > kMaxEntries)
        m_entries.removeLast();
}

QVariantList ClipboardHistory::entries(const QString &query) const
{
    QVariantList out;
    for (const QString &text : m_entries) {
        if (!query.isEmpty() && !text.contains(query, Qt::CaseInsensitive))
            continue;
        QVariantMap m;
        m.insert(QStringLiteral("text"), text);
        m.insert(QStringLiteral("preview"), previewOf(text));
        out.append(m);
    }
    return out;
}

void ClipboardHistory::setClipboard(const QString &text)
{
    if (text.isEmpty())
        return;
    m_ownText = text;
    if (QClipboard *cb = QGuiApplication::clipboard())
        cb->setText(text);
    pushEntry(text);
    save();
    emit changed();
}

void ClipboardHistory::clearHistory()
{
    if (m_entries.isEmpty()) {
        // Still make sure the file is emptied on disk.
        writeToFile(historyFilePath());
        return;
    }
    m_entries.clear();
    save();
    emit changed();
}

void ClipboardHistory::openInEditor()
{
    save(); // make sure the file reflects the current in-memory history
    QDesktopServices::openUrl(QUrl::fromLocalFile(historyFilePath()));
}

void ClipboardHistory::saveHistoryDialog()
{
    const QString path = QFileDialog::getSaveFileName(
        nullptr, tr("Guardar historial del portapapeles"),
        QDir::homePath() + QStringLiteral("/clipboard-history.txt"),
        tr("Archivos de texto (*.txt);;Todos los archivos (*)"));
    if (path.isEmpty())
        return;
    writeToFile(path);
}

void ClipboardHistory::load()
{
    QFile f(historyFilePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    const QString all = in.readAll();
    f.close();

    m_entries.clear();
    // Entries are separated by a delimiter line. The file begins with a
    // delimiter, so the first split chunk is empty and skipped.
    const QStringList chunks = all.split(kDelimiter + QLatin1Char('\n'));
    for (const QString &chunk : chunks) {
        if (chunk.isEmpty())
            continue;
        QString entry = chunk;
        // Drop the single trailing newline that precedes the next delimiter.
        if (entry.endsWith(QLatin1Char('\n')))
            entry.chop(1);
        if (!entry.isEmpty())
            m_entries.append(entry);
    }
    while (m_entries.size() > kMaxEntries)
        m_entries.removeLast();
}

void ClipboardHistory::save() const
{
    writeToFile(historyFilePath());
}

void ClipboardHistory::writeToFile(const QString &path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    for (const QString &entry : m_entries) {
        out << kDelimiter << '\n';
        out << entry << '\n';
    }
    f.close();
}
