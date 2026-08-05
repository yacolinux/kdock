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

void ThemeRowWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        emit activated();
    QWidget::mouseReleaseEvent(event);
}

void ThemeRowWidget::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void ThemeRowWidget::leaveEvent(QEvent *event)
{
    m_hovered = false;
    update();
    QWidget::leaveEvent(event);
}

void ThemeRowWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if (m_hovered) {
        QColor hl = palette().color(QPalette::Highlight);
        hl.setAlpha(45);
        p.fillRect(rect(), hl);
    }
    if (m_separator) {
        QColor line = palette().color(QPalette::WindowText);
        line.setAlpha(70);
        p.fillRect(QRect(0, 0, width(), 1), line);
    }
}

// ---------------------------------------------------------------- popup ----

ThemePickerPopup::ThemePickerPopup(AppearanceControl *appearance, const QString &kind, Mode mode,
                                   QWidget *parent)
    : QFrame(parent, Qt::Popup)
    , m_appearance(appearance)
    , m_kind(kind)
    , m_iconsMode(kind != QLatin1String("colors"))
    , m_mode(mode)
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

    // Counter on the left, "keep open" on the right: one line, and the checkbox
    // sits where the eye already goes before picking.
    {
        auto *head = new QHBoxLayout;
        head->setContentsMargins(0, 0, 0, 0);
        m_count = new QLabel(this);
        QFont small = m_count->font();
        small.setPointSizeF(small.pointSizeF() * 0.9);
        m_count->setFont(small);
        m_count->setEnabled(false); // reads as the dimmed caption the QML popup has
        head->addWidget(m_count);
        head->addStretch(1);

        m_keepOpen = new QCheckBox(tr("Mantener abierta"), this);
        m_keepOpen->setFont(small);
        m_keepOpen->setChecked(m_appearance && m_appearance->keepPickerOpen());
        m_keepOpen->setToolTip(tr("No cerrar esta ventana al elegir, para probar varios temas "
                                  "seguidos. Vale para todos los selectores."));
        connect(m_keepOpen, &QCheckBox::toggled, this, [this](bool on) {
            m_appearance->setKeepPickerOpen(on);
        });
        connect(m_appearance, &AppearanceControl::keepPickerOpenChanged, this, [this] {
            m_keepOpen->setChecked(m_appearance->keepPickerOpen());
        });
        head->addWidget(m_keepOpen);
        layout->addLayout(head);
    }

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

    // Backstop for a row whose widget has not been built yet (scrolled past
    // fast): those items are still plain, so the click reaches the list.
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!m_list->itemWidget(item))
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
    // Keeping the popup open after a choice means repopulating under the user:
    // without restoring the scroll the list jumps to the top, which reads as if
    // the popup had reset itself.
    const int scroll = m_list->verticalScrollBar()->value();
    m_list->clear();
    if (!m_appearance) {
        m_entries.clear();
        return;
    }

    const QVariantList all =
        m_iconsMode ? m_appearance->iconThemes() : m_appearance->colorSchemes();
    const QString needle = m_search->text().trimmed();
    m_entries.clear();
    // The do-nothing row ("(System default)", "(no cambiar)") leads the list and
    // is never filtered out by the search: it is how the user gets back to it.
    if (m_mode == PickValue && !m_specialLabel.isEmpty() && needle.isEmpty()) {
        m_entries.append(QVariantMap{{QStringLiteral("id"), QString()},
                                     {QStringLiteral("name"), m_specialLabel},
                                     {QStringLiteral("special"), true},
                                     {QStringLiteral("fav"), false}});
    }
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
    m_list->verticalScrollBar()->setValue(scroll);
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
    // In PickValue the tick follows the value being edited, not what KDE has on.
    const bool special = entry.value(QStringLiteral("special")).toBool();
    const bool current = m_mode == PickValue ? id == m_selectedId
                                             : entry.value(QStringLiteral("current")).toBool();

    auto *row = new ThemeRowWidget;
    row->setCursor(Qt::PointingHandCursor);
    connect(row, &ThemeRowWidget::activated, this, [this, entry] { applyEntry(entry); });
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(6, 0, 6, 0);
    layout->setSpacing(8);

    // Divider between the pinned block and the rest, drawn in paintEvent (a
    // stylesheet border would be skipped now that the row paints itself).
    row->setSeparator(firstNonFavorite);

    auto *preview = new QLabel(row);
    preview->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    preview->setFixedSize(previewWidth(), kPreviewSize);
    // The special row has nothing to preview, but still reserves the strip so
    // its label lines up with the rest.
    if (!special)
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

    if (special) {
        // No favourite for "(no cambiar)": it is not a theme. An empty
        // placeholder keeps the label column the same width as the other rows.
        auto *spacer = new QWidget(row);
        spacer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        spacer->setFixedWidth(QCheckBox().sizeHint().width());
        layout->addWidget(spacer);
    } else {
        auto *fav = new QCheckBox(row);
        fav->setChecked(entry.value(QStringLiteral("fav")).toBool());
        fav->setToolTip(tr("Favorito: lo pone al principio de la lista"));
        connect(fav, &QCheckBox::toggled, this, [this, id](bool on) {
            // Deliberately does not repopulate: the row would jump out from
            // under the cursor. The new order shows on the next open (and right
            // away in any other picker, which listens to favoritesChanged).
            m_appearance->setFavorite(m_kind, id, on);
        });
        layout->addWidget(fav);
    }

    return row;
}

void ThemePickerPopup::applyEntry(const QVariantMap &entry)
{
    const QString id = entry.value(QStringLiteral("id")).toString();
    if (m_mode == PickValue) {
        // An empty id is a real choice here (the special row), so no early out.
        m_selectedId = id;
        emit chosen(id);
    } else {
        if (id.isEmpty())
            return;
        if (m_iconsMode)
            m_appearance->applyIconTheme(id);
        else
            m_appearance->applyColorScheme(id);
    }

    if (m_appearance->keepPickerOpen()) {
        // Stay open to try the next one; repopulate so the tick moves (in
        // ApplyToDesktop the AppearanceControl::changed hook does it too, but
        // only once KDE reports back, and PickValue has no such hook).
        populate();
        return;
    }
    m_open = false;
    close();
}

// --------------------------------------------------------------- button ----

ThemePickerButton::ThemePickerButton(AppearanceControl *appearance, const QString &kind, Mode mode,
                                     QWidget *parent)
    : QPushButton(parent)
    , m_appearance(appearance)
    , m_kind(kind)
    , m_iconsMode(kind != QLatin1String("colors"))
    , m_mode(mode)
{
    refresh();
    // In PickValue the button's own value is the state; KDE changing its theme
    // is none of its business.
    if (m_mode == ThemePickerPopup::ApplyToDesktop)
        connect(m_appearance, &AppearanceControl::changed, this, &ThemePickerButton::refresh);
    // A favourite marked elsewhere reorders the list, and the display name of
    // an entry can only come from the list.
    connect(m_appearance, &AppearanceControl::favoritesChanged, this,
            &ThemePickerButton::refresh);
    connect(this, &QPushButton::clicked, this, [this] {
        if (!m_popup) {
            m_popup = new ThemePickerPopup(m_appearance, m_kind, m_mode, this);
            m_popup->setSpecialEntry(m_specialLabel);
            connect(m_popup, &ThemePickerPopup::chosen, this, [this](const QString &id) {
                if (id == m_currentId)
                    return;
                m_currentId = id;
                refresh();
                emit picked(id);
            });
        }
        m_popup->setSelectedId(m_currentId);
        m_popup->resize(qMax(width(), 320), m_popup->height());
        m_popup->showFor(this);
    });
}

void ThemePickerButton::setCurrentId(const QString &id)
{
    if (id == m_currentId)
        return;
    m_currentId = id;
    if (m_popup)
        m_popup->setSelectedId(id);
    refresh();
}

void ThemePickerButton::setSpecialEntry(const QString &label)
{
    m_specialLabel = label;
    if (m_popup)
        m_popup->setSpecialEntry(label);
    refresh();
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
    // ApplyToDesktop reads what KDE has on; PickValue carries its own value.
    if (m_mode == ThemePickerPopup::ApplyToDesktop) {
        m_currentId = m_iconsMode ? m_appearance->currentIconTheme()
                                  : m_appearance->currentColorScheme();
    }
    m_currentName.clear();
    m_preview = QPixmap();
    m_previewEntry.clear();

    if (!m_currentId.isEmpty()) {
        // Look the entry up so the button shows the display name (and, for a
        // scheme, the colors it needs to preview).
        const QVariantList all =
            m_iconsMode ? m_appearance->iconThemes() : m_appearance->colorSchemes();
        for (const QVariant &v : all) {
            const QVariantMap e = v.toMap();
            if (e.value(QStringLiteral("id")).toString() != m_currentId)
                continue;
            m_currentName = e.value(QStringLiteral("name")).toString();
            // The pixmap itself waits for the first paint: resolving three
            // icons costs ~12 ms, and the dialog builds 20 of these - most of
            // them on tabs the user may never open.
            m_previewEntry = e;
            break;
        }
        // A configured set that is not installed still has to show up, or the
        // button would claim a theme the dock is not actually using.
        if (m_currentName.isEmpty())
            m_currentName = tr("%1 (no instalado)").arg(m_currentId);
    } else if (!m_specialLabel.isEmpty()) {
        m_currentName = m_specialLabel; // "(System default)", "(no cambiar)"…
    } else {
        // kdeglobals with no ColorScheme key at all (a look-and-feel package
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

    // Resolved on the first paint, not in refresh(): see m_previewEntry.
    if (m_preview.isNull() && !m_previewEntry.isEmpty())
        m_preview = themePreviewPixmap(m_iconsMode, m_previewEntry, devicePixelRatioF());
    if (!m_preview.isNull()) {
        const int y = content.center().y() - kPreviewSize / 2 + 1;
        // Fade it while disabled (DarkMode's rows with their checkbox off): the
        // style greys out the text, but a full-colour preview beside it reads as
        // an enabled control.
        if (!isEnabled())
            p.setOpacity(0.4);
        p.drawPixmap(QPoint(content.left(), y), m_preview);
        p.setOpacity(1.0);
        content.setLeft(content.left() + previewWidth() + 8);
    }

    const QString text = fontMetrics().elidedText(m_currentName, Qt::ElideRight, content.width());
    p.drawItemText(content, Qt::AlignLeft | Qt::AlignVCenter, opt.palette, isEnabled(), text,
                   QPalette::ButtonText);
}
