#include "translations.h"

#include "desktopentry.h"
#include "dockconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QPair>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

namespace {

Translations *g_instance = nullptr;

const QLatin1String kSeparator(" = ");
const QLatin1String kDefaultLanguage("capabase");

// The .md is written by hand as often as it is written by us, so the parser is
// deliberately forgiving: anything that is not "<key> = <value>" under a known
// "## <section>" heading is a comment.
enum class Section { None, Text, Widgets, Apps };

Section sectionFor(const QString &title)
{
    const QString t = title.trimmed().toLower();
    if (t == QLatin1String("configuracion") || t == QLatin1String("configuración")
        || t == QLatin1String("uidock"))
        return Section::Text;
    if (t == QLatin1String("widgets"))
        return Section::Widgets;
    if (t == QLatin1String("apps"))
        return Section::Apps;
    return Section::None;
}

QString unescape(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (int i = 0; i < value.size(); ++i) {
        if (value.at(i) == QLatin1Char('\\') && i + 1 < value.size()) {
            const QChar next = value.at(++i);
            if (next == QLatin1Char('n'))
                out += QLatin1Char('\n');
            else if (next == QLatin1Char('t'))
                out += QLatin1Char('\t');
            else
                out += next; // covers "\\"
        } else {
            out += value.at(i);
        }
    }
    return out;
}

QString escape(const QString &value)
{
    QString out = value;
    out.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    out.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    return out;
}

// Leading/trailing spaces are part of several capabase strings (" px", "%1 "),
// so neither side of the separator is trimmed. Splitting on the *first* " = "
// is what keeps a key that ends in a space intact.
bool splitEntry(const QString &line, QString *key, QString *value)
{
    const int at = line.indexOf(kSeparator);
    if (at < 0)
        return false;
    *key = line.left(at);
    *value = line.mid(at + kSeparator.size());
    return !key->isEmpty();
}

struct ParsedFile
{
    QHash<QString, QString> text;
    QHash<QString, QString> widgets;
    QHash<QString, QString> apps;
};

ParsedFile parseFile(const QString &path)
{
    ParsedFile parsed;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return parsed;

    QTextStream stream(&file);
    Section section = Section::None;
    bool inComment = false;
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        const QString trimmed = line.trimmed();
        // HTML comments are the header block of every generated file, and they
        // do contain "=" signs.
        if (inComment) {
            if (trimmed.contains(QLatin1String("-->")))
                inComment = false;
            continue;
        }
        if (trimmed.startsWith(QLatin1String("<!--"))) {
            inComment = !trimmed.contains(QLatin1String("-->"));
            continue;
        }
        if (trimmed.startsWith(QLatin1String("##"))) {
            section = sectionFor(trimmed.mid(2));
            continue;
        }
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        QString key, value;
        if (!splitEntry(line, &key, &value) || value.isEmpty())
            continue; // an empty value means "use the default"
        switch (section) {
        case Section::Text:    parsed.text.insert(unescape(key), unescape(value)); break;
        case Section::Widgets: parsed.widgets.insert(key.trimmed(), unescape(value)); break;
        case Section::Apps:    parsed.apps.insert(key.trimmed(), unescape(value)); break;
        case Section::None:    break;
        }
    }
    return parsed;
}

QStringList readLines(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
}

// [start, end) of the body of "## <header>" in `lines`, or {-1, -1} if absent.
QPair<int, int> sectionBounds(const QStringList &lines, const QString &header)
{
    int start = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QString t = lines.at(i).trimmed();
        if (t.startsWith(QLatin1String("##"))
            && t.mid(2).trimmed().compare(header, Qt::CaseInsensitive) == 0) {
            start = i;
            break;
        }
    }
    if (start < 0)
        return {-1, -1};
    int end = lines.size();
    for (int i = start + 1; i < lines.size(); ++i) {
        if (lines.at(i).trimmed().startsWith(QLatin1String("##"))) {
            end = i;
            break;
        }
    }
    return {start, end};
}

// Copies "key = value" lines that exist under "## <header>" in the bundled
// catalog but are missing from the same section of the user's file, in place.
// This is how a translation seeded by an older kdock keeps gaining the
// strings later releases add (a new widget, a new dialog tab…) instead of
// silently falling back to capabase for them forever — seedFiles() below
// never overwrites an existing file wholesale, so without this a stale file
// on disk stays stale. Returns how many lines were added.
int mergeSection(QStringList &lines, const QStringList &bundledLines, const QString &header)
{
    const QPair<int, int> bundledRange = sectionBounds(bundledLines, header);
    if (bundledRange.first < 0)
        return 0;

    QPair<int, int> userRange = sectionBounds(lines, header);
    if (userRange.first < 0) {
        lines << QString() << (QStringLiteral("## ") + header);
        userRange = {lines.size() - 1, lines.size()};
    }

    QSet<QString> present;
    for (int i = userRange.first + 1; i < userRange.second; ++i) {
        QString key, value;
        if (splitEntry(lines.at(i), &key, &value))
            present.insert(key);
    }

    QStringList added;
    for (int i = bundledRange.first + 1; i < bundledRange.second; ++i) {
        QString key, value;
        if (!splitEntry(bundledLines.at(i), &key, &value) || present.contains(key))
            continue;
        present.insert(key);
        added << bundledLines.at(i);
    }
    if (added.isEmpty())
        return 0;

    int at = userRange.second;
    while (at > userRange.first + 1 && lines.at(at - 1).trimmed().isEmpty())
        --at;
    for (int i = 0; i < added.size(); ++i)
        lines.insert(at + i, added.at(i));
    return added.size();
}

QString languageSetting()
{
    QSettings settings(DockConfig::settingsFilePath(), QSettings::IniFormat);
    // QSettings folds the INI's [General] section into the root, so this is
    // value("language") and *not* value("General/language").
    const QString name = settings.value(QStringLiteral("language")).toString();
    return name.isEmpty() ? QString(kDefaultLanguage) : name;
}

} // namespace

QString KdockTranslator::translate(const char *context, const char *sourceText,
                                   const char *disambiguation, int n) const
{
    Q_UNUSED(context);
    Q_UNUSED(disambiguation);
    Q_UNUSED(n);
    if (!sourceText)
        return {};
    // A null QString is how a translator tells Qt "no translation": the caller
    // then keeps the source string, which is exactly the capabase fallback.
    return m_table.value(QString::fromUtf8(sourceText));
}

QString Translations::baseLanguage(const QString &name)
{
    const int at = name.indexOf(QLatin1String("-ALT-"));
    return at < 0 ? name : name.left(at);
}

Translations::Translations(Mode mode, QObject *parent)
    : QObject(parent)
    , m_mode(mode)
{
    g_instance = this;
    seedFiles();

    m_translator = new KdockTranslator(this);
    QCoreApplication::installTranslator(m_translator);

    m_active = resolved(languageSetting());
    loadActive();

    // Editing a file with the default editor has to show up in the dock without
    // a restart. Editors usually save by replacing the file, which drops the
    // watch, so the directory is watched and the path re-added on every hit.
    m_watcher = new QFileSystemWatcher(this);
    m_watcher->addPath(dirPath());
    // The accessory binaries do not own the setting: kdock writes it to the
    // shared kdock.conf and they have to follow, so the file itself is watched.
    if (m_mode == BaseOnly)
        m_watcher->addPath(DockConfig::settingsFilePath());
    auto *debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(250);
    connect(debounce, &QTimer::timeout, this, [this] {
        // In BaseOnly mode the trigger is usually the shared conf changing, i.e.
        // kdock switching language: re-read it before reloading.
        if (m_mode == BaseOnly)
            m_active = resolved(languageSetting());
        reload();
    });
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [debounce](const QString &) { debounce->start(); });
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this,
            [this, debounce](const QString &path) {
                if (!m_watcher->files().contains(path) && QFileInfo::exists(path))
                    m_watcher->addPath(path);
                debounce->start();
            });
}

Translations::~Translations()
{
    if (g_instance == this)
        g_instance = nullptr;
}

Translations *Translations::instance()
{
    return g_instance;
}

QString Translations::dirPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                        + QStringLiteral("/kdock/translations");
    QDir().mkpath(dir);
    return dir;
}

QString Translations::filePathFor(const QString &name)
{
    return dirPath() + QLatin1Char('/') + name + QStringLiteral(".md");
}

void Translations::seedFiles()
{
    // The shipped files live in the binary; on disk they are the user's, so an
    // existing one is never overwritten wholesale. But it IS topped up with any
    // Configuracion/UIdock/Widgets key the binary knows about and the file
    // doesn't (see mergeSection() above) — that is what makes a translation
    // seeded by an older kdock pick up the strings a newer release adds (e.g.
    // a previews/tilemenu dialog string) instead of falling back to capabase
    // for them forever. "Apps" is deliberately left out: that section is
    // populated by the user via the "Actualizar apps" button, not shipped.
    static const QStringList kMergedHeaders = { QStringLiteral("Configuracion"),
                                                  QStringLiteral("UIdock"),
                                                  QStringLiteral("Widgets") };
    const QDir bundled(QStringLiteral(":/translations"));
    for (const QFileInfo &info : bundled.entryInfoList({QStringLiteral("*.md")}, QDir::Files)) {
        const QString target = filePathFor(info.completeBaseName());
        if (!QFileInfo::exists(target)) {
            if (QFile::copy(info.absoluteFilePath(), target))
                QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner
                                              | QFile::ReadGroup | QFile::ReadOther);
            continue;
        }

        const QStringList bundledLines = readLines(info.absoluteFilePath());
        QStringList lines = readLines(target);
        int added = 0;
        for (const QString &header : kMergedHeaders)
            added += mergeSection(lines, bundledLines, header);
        if (added > 0) {
            QFile file(target);
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
                file.write(lines.join(QLatin1Char('\n')).toUtf8());
        }
    }
}

QStringList Translations::available() const
{
    QDir dir(dirPath());
    QStringList names;
    for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.md")}, QDir::Files, QDir::Name))
        names << info.completeBaseName();
    // capabase is the layer everything else falls back to: it heads the list.
    if (names.removeAll(QString(kDefaultLanguage)) > 0)
        names.prepend(QString(kDefaultLanguage));
    return names;
}

void Translations::loadActive()
{
    const ParsedFile parsed = parseFile(filePathFor(m_active));
    m_translator->setTable(parsed.text);
    m_widgets = parsed.widgets;
    m_apps = parsed.apps;
}

QString Translations::resolved(const QString &name) const
{
    return m_mode == BaseOnly ? baseLanguage(name) : name;
}

void Translations::setActive(const QString &name)
{
    // The name written to the config is the one the user picked; what this
    // process *loads* may be its base (see resolved()).
    if (name.isEmpty() || name == m_active)
        return;
    m_active = resolved(name);
    QSettings settings(DockConfig::settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("language"), name);
    loadActive();
    emit changed();
}

void Translations::reload()
{
    loadActive();
    emit changed();
}

QString Translations::widget(const QString &token) const
{
    return m_widgets.value(token);
}

QString Translations::app(const QString &desktopId) const
{
    return m_apps.value(desktopId);
}

int Translations::refreshApps(const QString &name, const QList<DesktopEntry> &entries)
{
    const QString path = filePathFor(name);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();

    // Find the Apps section (append it if the file predates it) and the line
    // where it ends, so the entries land inside it and not after some other
    // section the user may have added below.
    int start = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).trimmed().startsWith(QLatin1String("##"))
            && sectionFor(lines.at(i).trimmed().mid(2)) == Section::Apps) {
            start = i;
            break;
        }
    }
    if (start < 0) {
        lines << QString() << QStringLiteral("## Apps");
        start = lines.size() - 1;
    }
    int end = lines.size();
    for (int i = start + 1; i < lines.size(); ++i) {
        if (lines.at(i).trimmed().startsWith(QLatin1String("##"))) {
            end = i;
            break;
        }
    }

    QSet<QString> present;
    for (int i = start + 1; i < end; ++i) {
        QString key, value;
        if (splitEntry(lines.at(i), &key, &value))
            present.insert(key.trimmed());
    }

    QStringList added;
    for (const DesktopEntry &entry : entries) {
        if (entry.id.isEmpty() || entry.name.isEmpty() || present.contains(entry.id))
            continue;
        present.insert(entry.id);
        added << entry.id + kSeparator + escape(entry.name);
    }
    if (added.isEmpty())
        return 0;
    added.sort(Qt::CaseInsensitive);

    // Trailing blank lines of the section would push the new entries away from
    // it, so they are absorbed.
    int at = end;
    while (at > start + 1 && lines.at(at - 1).trimmed().isEmpty())
        --at;
    for (int i = 0; i < added.size(); ++i)
        lines.insert(at + i, added.at(i));

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return 0;
    file.write(lines.join(QLatin1Char('\n')).toUtf8());
    file.close();
    if (name == m_active)
        reload();
    return added.size();
}

QString appNameFor(const DesktopEntry &entry)
{
    if (Translations *layer = Translations::instance()) {
        const QString translated = layer->app(entry.id);
        if (!translated.isEmpty())
            return translated;
    }
    return entry.name;
}

bool Translations::createFrom(const QString &baseName, const QString &newName, QString *error)
{
    QString safe = newName.trimmed();
    for (QChar &c : safe) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_'))
            c = QLatin1Char('-');
    }
    if (safe.isEmpty()) {
        if (error)
            *error = tr("El nombre no puede estar vacío.");
        return false;
    }
    const QString target = filePathFor(safe);
    if (QFileInfo::exists(target)) {
        if (error)
            *error = tr("Ya existe una traducción llamada \"%1\".").arg(safe);
        return false;
    }
    if (!QFile::copy(filePathFor(baseName), target)) {
        if (error)
            *error = tr("No se pudo crear \"%1\".").arg(target);
        return false;
    }
    return true;
}
