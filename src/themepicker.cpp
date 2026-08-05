#include "themepicker.h"

#include "appearancecontrol.h"
#include "iconprovider.h"

#include <QApplication>
#include <QCheckBox>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QScreen>
#include <QScrollBar>
#include <QStyleOptionButton>
#include <QStylePainter>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kPreviewSize = 22;  // one sample icon / swatch
constexpr int kPreviewGap = 3;
constexpr int kRowHeight = 40;
constexpr int kPopupWidth = 380;
constexpr int kPopupHeight = 440;

// The three icons the QML popup previews with, so both look the same.
const char *const kSampleIcons[] = {"folder", "utilities-terminal", "configure"};
constexpr int kSampleCount = 3;

int previewWidth()
{
    return kSampleCount * kPreviewSize + (kSampleCount - 1) * kPreviewGap;
}

} // namespace

QPixmap themePreviewPixmap(bool iconsMode, const QVariantMap &entry, qreal dpr)
{
    const QString id = entry.value(QStringLiteral("id")).toString();
    // Keyed by dpr too: the same row can be drawn on screens with different
    // scaling while the dialog is dragged across monitors.
    static QHash<QString, QPixmap> cache;
    // The swatch outline follows the palette, so a scheme row drawn under a
    // different palette is a different pixmap.
    QColor outline = qApp->palette().color(QPalette::WindowText);
    outline.setAlpha(70);
    const QString key = (iconsMode ? QStringLiteral("i|") : QStringLiteral("c|")) + id
                        + QLatin1Char('|') + QString::number(dpr)
                        + (iconsMode ? QString() : QLatin1Char('|') + outline.name());
    const auto hit = cache.constFind(key);
    if (hit != cache.constEnd())
        return hit.value();

    QPixmap pm(QSize(previewWidth(), kPreviewSize) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    if (iconsMode) {
        for (int i = 0; i < kSampleCount; ++i) {
            // Resolve by file inside that theme's Inherits chain. Never
            // QIcon::setThemeName(): it is process-global state and swapping it
            // for one lookup leaks into everything else being drawn.
            const QString path = IconProvider::resolveInTheme(
                id, QString::fromLatin1(kSampleIcons[i]), kPreviewSize);
            if (path.isEmpty())
                continue;
            const QPixmap icon =
                QIcon(path).pixmap(QSize(kPreviewSize, kPreviewSize), dpr);
            p.drawPixmap(QPoint(i * (kPreviewSize + kPreviewGap), 0), icon);
        }
    } else {
        // The outline comes from the palette (computed above), not a fixed
        // black: on a dark color scheme a black swatch would have no edge.
        static const char *const kKeys[] = {"bg", "fg", "sel"};
        for (int i = 0; i < kSampleCount; ++i) {
            const QColor color(entry.value(QLatin1String(kKeys[i])).toString());
            QRectF r(i * (kPreviewSize + kPreviewGap), 0, kPreviewSize, kPreviewSize);
            r.adjust(0.5, 0.5, -0.5, -0.5);
            p.setPen(QPen(outline, 1));
            p.setBrush(color.isValid() ? color : QColor(Qt::transparent));
            p.drawRoundedRect(r, 4, 4);
        }
    }
    p.end();
    cache.insert(key, pm);
    return pm;
}

// ---------------------------------------------------------------- popup ----

ThemePickerPopup::ThemePickerPopup(AppearanceControl *appearance, const QString &kind,
                                   QWidget *parent)
    : QFrame(parent, Qt::Popup)
    , m_appearance(appearance)
    , m_kind(kind)
    , m_iconsMode(kind != QLatin1String("colors"))
{
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Raised);
    // Qt::Popup windows are not filled by the style on every platform.
    setAutoFillBackground(true);
    resize(kPopupWidth, kPopupHeight);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(m_iconsMode ? tr("Buscar iconset…")
                                             : tr("Buscar esquema de color…"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_count = new QLabel(this);
    QFont small = m_count->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    m_count->setFont(small);
    m_count->setEnabled(false); // reads as the dimmed caption the QML popup has
    layout->addWidget(m_count);

    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->viewport()->installEventFilter(this);
    layout->addWidget(m_list, 1);

    // Debounced search, same reason as IconPickerDialog: repopulating rebuilds
    // the row widgets.
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);
    connect(m_debounce, &QTimer::timeout, this, &ThemePickerPopup::populate);
    connect(m_search, &QLineEdit::textChanged, this, [this] { m_debounce->start(); });
    // Enter applies the first match: with 456 schemes, typing the name and
    // hitting return is the fast path.
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        if (!m_entries.isEmpty())
            applyEntry(m_entries.first().toMap());
    });

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        applyEntry(item->data(Qt::UserRole).toMap());
    });
    // Rows are built as they scroll into view.
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] { buildVisibleRows(); });

    // Another picker (the dock widget, or the other button) changed something.
    connect(m_appearance, &AppearanceControl::changed, this, [this] {
        if (m_open)
            populate();
    });
}

void ThemePickerPopup::showFor(QWidget *anchor)
{
    if (!m_appearance)
        return;
    if (!m_iconsMode)
        m_appearance->refreshIfStale(); // cheap unless a scheme was installed

    m_open = true;
    m_search->clear();          // also fires populate() through the debounce…
    m_debounce->stop();
    populate();                 // …but the popup has to be filled right now

    // Below the anchor, flipped above when the screen has no room.
    const QPoint below = anchor->mapToGlobal(QPoint(0, anchor->height() + 2));
    QPoint pos = below;
    const QRect screen = anchor->screen() ? anchor->screen()->availableGeometry() : QRect();
    if (screen.isValid()) {
        if (pos.y() + height() > screen.bottom())
            pos.setY(anchor->mapToGlobal(QPoint(0, 0)).y() - height() - 2);
        if (pos.x() + width() > screen.right())
            pos.setX(screen.right() - width());
        pos.setX(qMax(pos.x(), screen.left()));
        pos.setY(qMax(pos.y(), screen.top()));
    }
    move(pos);
    show();
    m_search->setFocus();
    // The viewport is only its final size now, so the rows the user actually
    // sees are built here (populate() above ran against a one-row viewport).
    buildVisibleRows();
}

void ThemePickerPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QFrame::keyPressEvent(event);
}

bool ThemePickerPopup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_list->viewport() && event->type() == QEvent::Resize)
        buildVisibleRows(); // a taller viewport shows rows that had no widget yet
    return QFrame::eventFilter(watched, event);
}

void ThemePickerPopup::populate()
{
    m_list->clear();
    if (!m_appearance) {
        m_entries.clear();
        return;
    }

    const QVariantList all =
        m_iconsMode ? m_appearance->iconThemes() : m_appearance->colorSchemes();
    const QString needle = m_search->text().trimmed();
    m_entries.clear();
    for (const QVariant &v : all) {
        const QVariantMap e = v.toMap();
        if (!needle.isEmpty()
            && !e.value(QStringLiteral("name")).toString().contains(needle, Qt::CaseInsensitive)
            && !e.value(QStringLiteral("id")).toString().contains(needle, Qt::CaseInsensitive))
            continue;
        m_entries.append(e);
    }

    m_count->setText(m_iconsMode ? tr("%1 iconsets").arg(m_entries.size())
                                 : tr("%1 esquemas").arg(m_entries.size()));

    bool previousWasFavorite = false;
    for (int i = 0; i < m_entries.size(); ++i) {
        const QVariantMap e = m_entries.at(i).toMap();
        const bool fav = e.value(QStringLiteral("fav")).toBool();
        auto *item = new QListWidgetItem(m_list);
        item->setSizeHint(QSize(0, kRowHeight));
        item->setData(Qt::UserRole, e);
        // Marks the first row after the favourites block, which draws the
        // separator line.
        item->setData(Qt::UserRole + 1, previousWasFavorite && !fav);
        previousWasFavorite = fav;
    }
    buildVisibleRows();
}

void ThemePickerPopup::buildVisibleRows()
{
    if (m_list->count() == 0)
        return;
    const QRect viewport = m_list->viewport()->rect();
    // One row above and below, so a slow scroll never shows an empty band.
    const int first = qMax(0, m_list->row(m_list->itemAt(viewport.topLeft() + QPoint(2, 2))) - 1);
    QListWidgetItem *lastVisible = m_list->itemAt(viewport.bottomLeft() + QPoint(2, -2));
    const int last = lastVisible ? qMin(m_list->count() - 1, m_list->row(lastVisible) + 1)
                                : m_list->count() - 1;

    for (int i = first; i <= last; ++i) {
        QListWidgetItem *item = m_list->item(i);
        if (!item || m_list->itemWidget(item))
            continue;
        m_list->setItemWidget(item, makeRow(item->data(Qt::UserRole).toMap(),
                                            item->data(Qt::UserRole + 1).toBool()));
    }
}

QWidget *ThemePickerPopup::makeRow(const QVariantMap &entry, bool firstNonFavorite)
{
    const QString id = entry.value(QStringLiteral("id")).toString();
    const bool current = entry.value(QStringLiteral("current")).toBool();

    auto *row = new QWidget;
    // The row must not eat the click: the list applies the entry through
    // itemClicked. Only the checkbox stays opaque to the mouse, so ticking a
    // favourite does not also change the desktop's theme.
    row->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(6, 0, 6, 0);
    layout->setSpacing(8);

    if (firstNonFavorite) {
        // A top border on the first non-favourite row is the separator between
        // the pinned block and the rest. Scoped by object name: a bare "QWidget"
        // selector would give every child label a border of its own.
        row->setObjectName(QStringLiteral("themeRowSeparated"));
        row->setStyleSheet(
            QStringLiteral("#themeRowSeparated { border-top: 1px solid palette(mid); }"));
    }

    auto *preview = new QLabel(row);
    preview->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    preview->setFixedSize(previewWidth(), kPreviewSize);
    preview->setPixmap(themePreviewPixmap(m_iconsMode, entry, devicePixelRatioF()));
    layout->addWidget(preview);

    auto *name = new QLabel(entry.value(QStringLiteral("name")).toString(), row);
    name->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    name->setToolTip(id);
    if (current) {
        QFont f = name->font();
        f.setBold(true);
        name->setFont(f);
    }
    layout->addWidget(name, 1);

    if (current) {
        auto *tick = new QLabel(row);
        tick->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        tick->setPixmap(QIcon::fromTheme(QStringLiteral("dialog-ok")).pixmap(16, 16));
        layout->addWidget(tick);
    }

    auto *fav = new QCheckBox(row);
    fav->setChecked(entry.value(QStringLiteral("fav")).toBool());
    fav->setToolTip(tr("Favorito: lo pone al principio de la lista"));
    connect(fav, &QCheckBox::toggled, this, [this, id](bool on) {
        // Deliberately does not repopulate: the row would jump out from under
        // the cursor. The new order shows on the next open (and right away in
        // any other picker, which listens to favoritesChanged).
        m_appearance->setFavorite(m_kind, id, on);
    });
    layout->addWidget(fav);

    return row;
}

void ThemePickerPopup::applyEntry(const QVariantMap &entry)
{
    const QString id = entry.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return;
    if (m_iconsMode)
        m_appearance->applyIconTheme(id);
    else
        m_appearance->applyColorScheme(id);
    m_open = false;
    close();
}

// --------------------------------------------------------------- button ----

ThemePickerButton::ThemePickerButton(AppearanceControl *appearance, const QString &kind,
                                     QWidget *parent)
    : QPushButton(parent)
    , m_appearance(appearance)
    , m_kind(kind)
    , m_iconsMode(kind != QLatin1String("colors"))
{
    refresh();
    connect(m_appearance, &AppearanceControl::changed, this, &ThemePickerButton::refresh);
    connect(this, &QPushButton::clicked, this, [this] {
        if (!m_popup)
            m_popup = new ThemePickerPopup(m_appearance, m_kind, this);
        m_popup->resize(qMax(width(), 320), m_popup->height());
        m_popup->showFor(this);
    });
}

QSize ThemePickerButton::sizeHint() const
{
    const QSize base = QPushButton::sizeHint();
    // Wide enough for the preview strip plus a typical theme name: the button
    // elides, and these names ("2026-Buuf For Many Desktops") run long.
    return QSize(qMax(base.width(), 300), qMax(base.height(), kPreviewSize + 12));
}

void ThemePickerButton::refresh()
{
    if (!m_appearance)
        return;
    const QString currentId = m_iconsMode ? m_appearance->currentIconTheme()
                                          : m_appearance->currentColorScheme();
    m_currentName.clear();
    m_preview = QPixmap();

    if (!currentId.isEmpty()) {
        // Look the entry up so the button shows the display name (and, for a
        // scheme, the colors it needs to preview).
        const QVariantList all =
            m_iconsMode ? m_appearance->iconThemes() : m_appearance->colorSchemes();
        for (const QVariant &v : all) {
            const QVariantMap e = v.toMap();
            if (e.value(QStringLiteral("id")).toString() != currentId)
                continue;
            m_currentName = e.value(QStringLiteral("name")).toString();
            m_preview = themePreviewPixmap(m_iconsMode, e, devicePixelRatioF());
            break;
        }
        if (m_currentName.isEmpty())
            m_currentName = currentId; // installed set gone, or an unknown id
    } else {
        // kdeglobals without General/ColorScheme (a look-and-feel package
        // applied the scheme): nothing is ticked and nothing is named.
        m_currentName = tr("(sin definir)");
    }
    update();
}

void ThemePickerButton::paintEvent(QPaintEvent *)
{
    QStylePainter p(this);
    QStyleOptionButton opt;
    initStyleOption(&opt);
    opt.text.clear(); // the label is drawn by hand below
    opt.icon = QIcon();
    p.drawControl(QStyle::CE_PushButton, opt);

    QRect content = style()->subElementRect(QStyle::SE_PushButtonContents, &opt, this);
    content.adjust(4, 0, -4, 0);

    // Drop-down arrow on the right, so the control reads as a dropdown.
    QStyleOption arrowOpt;
    arrowOpt.rect = QRect(content.right() - 12, content.center().y() - 4, 12, 8);
    arrowOpt.palette = opt.palette;
    arrowOpt.state = opt.state;
    p.drawPrimitive(QStyle::PE_IndicatorArrowDown, arrowOpt);
    content.setRight(arrowOpt.rect.left() - 6);

    if (!m_preview.isNull()) {
        const int y = content.center().y() - kPreviewSize / 2 + 1;
        p.drawPixmap(QPoint(content.left(), y), m_preview);
        content.setLeft(content.left() + previewWidth() + 8);
    }

    const QString text = fontMetrics().elidedText(m_currentName, Qt::ElideRight, content.width());
    p.drawItemText(content, Qt::AlignLeft | Qt::AlignVCenter, opt.palette, isEnabled(), text,
                   QPalette::ButtonText);
}
