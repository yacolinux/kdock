#include "iconpickerdialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// Themes to scan: the active theme plus everything it inherits, the fallback,
// and hicolor (the freedesktop base theme). Follows Inherits= transitively.
QStringList themesToScan(const QStringList &searchPaths)
{
    QStringList queue;
    if (!QIcon::themeName().isEmpty())
        queue << QIcon::themeName();
    if (!QIcon::fallbackThemeName().isEmpty())
        queue << QIcon::fallbackThemeName();
    queue << QStringLiteral("breeze") << QStringLiteral("hicolor");

    QStringList out;
    QSet<QString> seen;
    while (!queue.isEmpty()) {
        const QString theme = queue.takeFirst();
        if (theme.isEmpty() || seen.contains(theme))
            continue;
        seen.insert(theme);
        out << theme;
        // Find this theme's index.theme to read Inherits=.
        for (const QString &base : searchPaths) {
            const QString index = base + QLatin1Char('/') + theme
                                  + QStringLiteral("/index.theme");
            if (QFileInfo::exists(index)) {
                QSettings ini(index, QSettings::IniFormat);
                ini.beginGroup(QStringLiteral("Icon Theme"));
                const QStringList inh =
                    ini.value(QStringLiteral("Inherits")).toStringList();
                ini.endGroup();
                for (const QString &p : inh)
                    if (!seen.contains(p))
                        queue << p;
                break;
            }
        }
    }
    return out;
}

} // namespace

QStringList IconPickerDialog::availableIconNames()
{
    static QStringList cache;
    if (!cache.isEmpty())
        return cache;

    QStringList searchPaths = QIcon::themeSearchPaths();
    // Also the XDG standard locations, in case the theme search paths are lean.
    for (const QString &dir :
         QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                   QStringLiteral("icons"),
                                   QStandardPaths::LocateDirectory)) {
        if (!searchPaths.contains(dir))
            searchPaths << dir;
    }
    searchPaths.removeDuplicates();

    static const QStringList kExts = {QStringLiteral("png"), QStringLiteral("svg"),
                                      QStringLiteral("svgz"), QStringLiteral("xpm")};
    QSet<QString> names;
    for (const QString &theme : themesToScan(searchPaths)) {
        for (const QString &base : searchPaths) {
            const QString root = base + QLatin1Char('/') + theme;
            if (!QFileInfo::exists(root))
                continue;
            QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                const QFileInfo fi = it.fileInfo();
                if (kExts.contains(fi.suffix().toLower()))
                    names.insert(fi.completeBaseName());
            }
        }
    }

    cache = names.values();
    cache.sort(Qt::CaseInsensitive);
    return cache;
}

IconPickerDialog::IconPickerDialog(const QString &current, QWidget *parent)
    : QDialog(parent)
    , m_current(current)
    , m_selected(current)
{
    setWindowTitle(tr("Choose Icon"));
    resize(560, 460);

    auto *layout = new QVBoxLayout(this);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search icons…"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setUniformItemSizes(true);
    m_list->setIconSize(QSize(32, 32));
    m_list->setGridSize(QSize(84, 72));
    m_list->setWordWrap(true);
    layout->addWidget(m_list, 1);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Debounced search so typing doesn't rebuild the (large) grid on every key.
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);
    connect(m_debounce, &QTimer::timeout, this,
            [this] { populate(m_search->text()); });
    connect(m_search, &QLineEdit::textChanged, this,
            [this] { m_debounce->start(); });

    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item)
            m_selected = item->data(Qt::UserRole).toString();
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        m_selected = item->data(Qt::UserRole).toString();
        accept();
    });

    populate(QString());
}

void IconPickerDialog::populate(const QString &filter)
{
    m_list->clear();

    const QStringList all = availableIconNames();
    const QString needle = filter.trimmed();
    constexpr int kCap = 2000; // keep the grid responsive
    int shown = 0;
    QListWidgetItem *toSelect = nullptr;

    for (const QString &name : all) {
        if (!needle.isEmpty() && !name.contains(needle, Qt::CaseInsensitive))
            continue;
        auto *item = new QListWidgetItem(QIcon::fromTheme(name), name, m_list);
        item->setData(Qt::UserRole, name);
        item->setToolTip(name);
        if (name == m_current)
            toSelect = item;
        if (++shown >= kCap)
            break;
    }

    if (toSelect) {
        m_list->setCurrentItem(toSelect);
        m_list->scrollToItem(toSelect, QAbstractItemView::PositionAtCenter);
    }
}
