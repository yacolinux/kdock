#include "clipboardhistory.h"

#include "dockconfig.h"
#include "waylandclipboard.h"

#include <QBuffer>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QSettings>
#include <QStandardPaths>
#include <QSaveFile>
#include <QThread>
#include <QTextStream>
#include <QUrl>

namespace {
// Delimiter line between entries in the plain-text export. Kept human-readable
// so "Ver historial actual" (open in editor) is legible.
const QString kDelimiter = QStringLiteral("=== kdock clipboard entry ===");

QString snapshotPreviewOf(const ClipboardHistory::Entry &entry)
{
    if (entry.isImage())
        return entry.imageSize.isValid()
                   ? QObject::tr("Imagen %1 × %2").arg(entry.imageSize.width())
                         .arg(entry.imageSize.height())
                   : QObject::tr("Imagen");
    QString t = entry.text.trimmed();
    const int nl = t.indexOf(QLatin1Char('\n'));
    if (nl >= 0)
        t = t.left(nl) + QStringLiteral(" …");
    if (t.size() > 120)
        t = t.left(120) + QStringLiteral("…");
    return t;
}

QString dataDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                        + QStringLiteral("/kdock");
    QDir().mkpath(dir);
    return dir;
}

void writeSnapshot(const QList<ClipboardHistory::Entry> &entries,
                   const QString &indexPath, const QString &textPath)
{
    QJsonArray array;
    for (const ClipboardHistory::Entry &e : entries) {
        QJsonObject o;
        if (e.isImage()) {
            o.insert(QStringLiteral("type"), QStringLiteral("image"));
            o.insert(QStringLiteral("file"), e.imageFile);
            o.insert(QStringLiteral("w"), e.imageSize.width());
            o.insert(QStringLiteral("h"), e.imageSize.height());
        } else {
            o.insert(QStringLiteral("type"), QStringLiteral("text"));
            o.insert(QStringLiteral("text"), e.text);
        }
        array.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("entries"), array);

    QSaveFile index(indexPath);
    if (index.open(QIODevice::WriteOnly)) {
        index.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        index.commit();
    }

    QSaveFile text(textPath);
    if (!text.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream out(&text);
    out.setEncoding(QStringConverter::Utf8);
    for (const ClipboardHistory::Entry &e : entries) {
        out << kDelimiter << '\n';
        if (e.isImage())
            out << QStringLiteral("[%1 — clipboard-images/%2]")
                       .arg(snapshotPreviewOf(e), e.imageFile) << '\n';
        else
            out << e.text << '\n';
    }
    text.commit();
}
} // namespace

ClipboardHistory::ClipboardHistory(QObject *parent)
    : QObject(parent)
{
    static const auto captureImagesKey = QStringLiteral("clipboard/captureImages");
    QSettings shared(DockConfig::settingsFilePath(), QSettings::IniFormat);
    if (shared.contains(captureImagesKey)) {
        m_captureImages = shared.value(captureImagesKey, true).toBool();
    } else {
        // Migrate the old platform-default QSettings value into the shared
        // file so complete configuration archives carry it from now on.
        QSettings legacy(QStringLiteral("kdock"), QStringLiteral("kdock"));
        m_captureImages = legacy.value(captureImagesKey, true).toBool();
        if (legacy.contains(captureImagesKey))
            shared.setValue(captureImagesKey, m_captureImages);
    }

    load();

    // Preferred backend: focus-independent capture over ext-data-control.
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        m_wayland = new WaylandClipboard(this);
        connect(m_wayland, &WaylandClipboard::textCopied, this, [this](const QString &text) {
            if (text.isEmpty() || (!m_entries.isEmpty() && m_entries.first().text == text))
                return;
            pushText(text);
            save();
            emit changed();
        });
        connect(m_wayland, &WaylandClipboard::imageCopied, this,
                [this](const QByteArray &data, const QString &) {
                    if (!m_captureImages)
                        return;
                    if (pushImage(data)) {
                        save();
                        emit changed();
                    }
                });
    }

    // Fallback (and seed): QClipboard only reports changes while the accessory
    // holds keyboard focus on Wayland; ext-data-control is the passive path.
    if (QClipboard *cb = QGuiApplication::clipboard()) {
        connect(cb, &QClipboard::dataChanged, this, &ClipboardHistory::captureClipboard);
        captureClipboard();
    }
}

ClipboardHistory::~ClipboardHistory()
{
    // The worker only owns its immutable snapshot and does not call back into
    // this object. Wait before QObject destroys the QThread child; otherwise a
    // accessory restart while a large history is being flushed would terminate the
    // process from QThread's destructor.
    if (m_saveThread)
        m_saveThread->wait();
}

QString ClipboardHistory::historyFilePath()
{
    return dataDir() + QStringLiteral("/clipboard-history.txt");
}

QString ClipboardHistory::indexFilePath()
{
    return dataDir() + QStringLiteral("/clipboard-history.json");
}

QString ClipboardHistory::imagesDirPath()
{
    const QString dir = dataDir() + QStringLiteral("/clipboard-images");
    QDir().mkpath(dir);
    return dir;
}

void ClipboardHistory::setCaptureImages(bool on)
{
    if (m_captureImages == on)
        return;
    m_captureImages = on;
    QSettings settings(DockConfig::settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("clipboard/captureImages"), on);
    emit captureImagesChanged();
}

QString ClipboardHistory::previewOf(const Entry &entry)
{
    if (entry.isImage()) {
        return entry.imageSize.isValid()
                   ? QObject::tr("Imagen %1 × %2").arg(entry.imageSize.width())
                         .arg(entry.imageSize.height())
                   : QObject::tr("Imagen");
    }
    QString t = entry.text.trimmed();
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
    // On Wayland the data-control backend already saw this (and it sees the
    // copies this path is blind to), so leave it alone.
    if (m_wayland && m_wayland->active())
        return;

    const QString text = cb->text();
    if (!text.isEmpty()) {
        // Ignore the echo from our own setClipboard().
        if (text == m_ownText)
            return;
        if (!m_entries.isEmpty() && m_entries.first().text == text)
            return;
        pushText(text);
        save();
        emit changed();
        return;
    }

    if (!m_captureImages)
        return;
    const QMimeData *mime = cb->mimeData();
    if (!mime || !mime->hasImage())
        return;
    const QImage image = qvariant_cast<QImage>(mime->imageData());
    if (image.isNull())
        return;
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    if (pushImage(png)) {
        save();
        emit changed();
    }
}

void ClipboardHistory::pushText(const QString &text)
{
    Entry e;
    e.text = text;
    pushEntry(e);
}

bool ClipboardHistory::pushImage(const QByteArray &data)
{
    if (data.isEmpty() || data.size() > kMaxImageBytes)
        return false;

    // Read the header only: the dimensions and the format are all we need to
    // decide, and decoding a 4K screenshot on the GUI thread is not free.
    QBuffer probe;
    probe.setData(data);
    probe.open(QIODevice::ReadOnly);
    QImageReader reader(&probe);
    const QSize size = reader.size();
    const QByteArray format = reader.format().toLower();
    if (!size.isValid())
        return false;

    // PNG is stored verbatim; anything else is transcoded so the history holds
    // one format only.
    QByteArray png = data;
    if (format != "png") {
        const QImage image = reader.read();
        if (image.isNull())
            return false;
        png.clear();
        QBuffer out(&png);
        out.open(QIODevice::WriteOnly);
        if (!image.save(&out, "PNG"))
            return false;
    }

    const QString name = QStringLiteral("img-%1.png")
                             .arg(QString::fromLatin1(
                                 QCryptographicHash::hash(png, QCryptographicHash::Sha1).toHex()));
    const QString path = imagesDirPath() + QLatin1Char('/') + name;
    if (!QFile::exists(path)) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(png);
        f.close();
    }

    Entry e;
    e.imageFile = name;
    e.imageSize = size;
    pushEntry(e);
    return true;
}

void ClipboardHistory::pushEntry(const Entry &entry)
{
    // Dedup: an existing copy moves to the top.
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        const Entry &e = m_entries.at(i);
        const bool same = entry.isImage() ? e.imageFile == entry.imageFile
                                          : (!e.isImage() && e.text == entry.text);
        if (same) {
            m_entries.removeAt(i);
            break;
        }
    }
    m_entries.prepend(entry);
    trim();
}

void ClipboardHistory::trim()
{
    while (m_entries.size() > kMaxEntries)
        m_entries.removeLast();

    int images = 0;
    for (qsizetype i = 0; i < m_entries.size();) {
        if (m_entries.at(i).isImage() && ++images > kMaxImages)
            m_entries.removeAt(i);
        else
            ++i;
    }
}

QVariantList ClipboardHistory::entries(const QString &query) const
{
    QVariantList out;
    for (const Entry &entry : m_entries) {
        if (!query.isEmpty()
            && (entry.isImage() || !entry.text.contains(query, Qt::CaseInsensitive)))
            continue;
        QVariantMap m;
        m.insert(QStringLiteral("text"), entry.text);
        m.insert(QStringLiteral("preview"), previewOf(entry));
        m.insert(QStringLiteral("isImage"), entry.isImage());
        m.insert(QStringLiteral("file"), entry.imageFile);
        m.insert(QStringLiteral("imageUrl"),
                 entry.isImage() ? QUrl::fromLocalFile(imagesDirPath() + QLatin1Char('/')
                                                       + entry.imageFile).toString()
                                 : QString());
        m.insert(QStringLiteral("width"), entry.imageSize.width());
        m.insert(QStringLiteral("height"), entry.imageSize.height());
        out.append(m);
    }
    return out;
}

void ClipboardHistory::setClipboard(const QString &text)
{
    if (text.isEmpty())
        return;
    m_ownText = text;
    if (m_wayland && m_wayland->active())
        m_wayland->setText(text);
    else if (QClipboard *cb = QGuiApplication::clipboard())
        cb->setText(text);
    pushText(text);
    saveAsync();
    emit changed();
}

void ClipboardHistory::setClipboardImage(const QString &fileName)
{
    if (fileName.isEmpty())
        return;
    QFile f(imagesDirPath() + QLatin1Char('/') + fileName);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray png = f.readAll();
    f.close();

    if (m_wayland && m_wayland->active()) {
        m_wayland->setImage(png);
    } else if (QClipboard *cb = QGuiApplication::clipboard()) {
        QImage image;
        image.loadFromData(png, "PNG");
        if (!image.isNull())
            cb->setImage(image);
    }

    // Move it to the top without re-reading the file.
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).imageFile == fileName) {
            const Entry e = m_entries.takeAt(i);
            m_entries.prepend(e);
            break;
        }
    }
    saveAsync();
    emit changed();
}

void ClipboardHistory::saveAsync()
{
    m_saveAgain = true;
    if (m_saveThread)
        return;

    const QList<Entry> snapshot = m_entries;
    const QString indexPath = indexFilePath();
    const QString textPath = historyFilePath();
    m_saveAgain = false;
    m_saveThread = QThread::create([snapshot, indexPath, textPath] {
        writeSnapshot(snapshot, indexPath, textPath);
    });
    m_saveThread->setParent(this);
    connect(m_saveThread, &QThread::finished, this, [this] {
        QThread *thread = m_saveThread;
        m_saveThread = nullptr;
        thread->deleteLater();
        if (m_saveAgain)
            saveAsync();
    });
    m_saveThread->start();
}

void ClipboardHistory::clearHistory()
{
    m_entries.clear();
    save();          // truncates the index and the text export
    sweepOrphanImages(); // nothing is referenced now: removes every PNG
    emit changed();
}

void ClipboardHistory::openInEditor()
{
    save(); // make sure the export reflects the current in-memory history
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
    writeTextExport(path);
}

void ClipboardHistory::load()
{
    m_entries.clear();

    QFile index(indexFilePath());
    if (index.open(QIODevice::ReadOnly)) {
        const QJsonArray array =
            QJsonDocument::fromJson(index.readAll()).object()
                .value(QStringLiteral("entries")).toArray();
        index.close();
        for (const QJsonValue &value : array) {
            const QJsonObject o = value.toObject();
            Entry e;
            if (o.value(QStringLiteral("type")).toString() == QLatin1String("image")) {
                e.imageFile = o.value(QStringLiteral("file")).toString();
                e.imageSize = QSize(o.value(QStringLiteral("w")).toInt(),
                                    o.value(QStringLiteral("h")).toInt());
                // A PNG deleted behind our back would show as a broken row.
                if (e.imageFile.isEmpty()
                    || !QFile::exists(imagesDirPath() + QLatin1Char('/') + e.imageFile))
                    continue;
            } else {
                e.text = o.value(QStringLiteral("text")).toString();
                if (e.text.isEmpty())
                    continue;
            }
            m_entries.append(e);
        }
        trim();
        sweepOrphanImages();
        return;
    }

    // Migration: before the index existed the history was the plain-text file,
    // whose entries are separated by a delimiter line. The file begins with a
    // delimiter, so the first split chunk is empty and skipped.
    QFile legacy(historyFilePath());
    if (!legacy.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QTextStream in(&legacy);
    in.setEncoding(QStringConverter::Utf8);
    const QString all = in.readAll();
    legacy.close();

    const QStringList chunks = all.split(kDelimiter + QLatin1Char('\n'));
    for (const QString &chunk : chunks) {
        if (chunk.isEmpty())
            continue;
        QString text = chunk;
        // Drop the single trailing newline that precedes the next delimiter.
        if (text.endsWith(QLatin1Char('\n')))
            text.chop(1);
        if (text.isEmpty())
            continue;
        Entry e;
        e.text = text;
        m_entries.append(e);
    }
    trim();
}

void ClipboardHistory::save() const
{
    writeIndex();
    writeTextExport(historyFilePath());
}

void ClipboardHistory::writeIndex() const
{
    QJsonArray array;
    for (const Entry &e : m_entries) {
        QJsonObject o;
        if (e.isImage()) {
            o.insert(QStringLiteral("type"), QStringLiteral("image"));
            o.insert(QStringLiteral("file"), e.imageFile);
            o.insert(QStringLiteral("w"), e.imageSize.width());
            o.insert(QStringLiteral("h"), e.imageSize.height());
        } else {
            o.insert(QStringLiteral("type"), QStringLiteral("text"));
            o.insert(QStringLiteral("text"), e.text);
        }
        array.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("entries"), array);

    QFile f(indexFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void ClipboardHistory::writeTextExport(const QString &path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    for (const Entry &e : m_entries) {
        out << kDelimiter << '\n';
        if (e.isImage())
            out << QStringLiteral("[%1 — clipboard-images/%2]")
                       .arg(previewOf(e), e.imageFile) << '\n';
        else
            out << e.text << '\n';
    }
}

void ClipboardHistory::sweepOrphanImages() const
{
    QStringList referenced;
    for (const Entry &e : m_entries) {
        if (e.isImage())
            referenced.append(e.imageFile);
    }
    QDir dir(imagesDirPath());
    const QStringList files = dir.entryList({QStringLiteral("img-*.png")}, QDir::Files);
    for (const QString &name : files) {
        if (!referenced.contains(name))
            dir.remove(name);
    }
}
