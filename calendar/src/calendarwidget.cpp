#include "calendarwidget.h"

#include <QEvent>
#include <QFont>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

namespace {

// Vertical budget, top to bottom. The day grid takes whatever is left, which is
// what makes it *wide*: seven equal columns whose height follows the window.
constexpr int kPad = 16;
constexpr int kHeaderH = 70;
constexpr int kWeekdayH = 26;
constexpr int kFooterH = 46;
constexpr int kArrowW = 44;
constexpr int kTodayBtnW = 66;

// Soft-fill used behind hovered arrows/buttons and hovered days. Alpha is low
// on purpose: KDE highlights selection, not the whole surroundings.
constexpr int kHoverAlpha = 38;

} // namespace

CalendarWidget::CalendarWidget(QWidget *parent)
    : QWidget(parent)
    , m_today(QDate::currentDate())
    , m_selected(m_today)
{
    setMinimumSize(360, 430);
    setMouseTracking(true); // hover without a button held
    setFocusPolicy(Qt::StrongFocus); // arrow-key navigation from the start
    setMonth(m_today);
}

void CalendarWidget::setMonth(const QDate &firstOfMonth)
{
    const QDate first(firstOfMonth.year(), firstOfMonth.month(), 1);
    if (first == m_month)
        return;
    m_month = first;
    setWindowTitle(windowTitleFor(first));
    emit monthChanged(m_month);
    update();
}

void CalendarWidget::goToToday()
{
    m_selected = m_today;
    setMonth(m_today);
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

QRect CalendarWidget::headerRect() const
{
    return QRect(kPad, kPad, width() - 2 * kPad, kHeaderH);
}

QRect CalendarWidget::weekdayRect() const
{
    const QRect h = headerRect();
    return QRect(h.left(), h.bottom() + 2, h.width(), kWeekdayH);
}

QRect CalendarWidget::gridRect() const
{
    const QRect w = weekdayRect();
    const int bottom = height() - kPad - kFooterH;
    const int top = w.bottom() + 2;
    return QRect(w.left(), top, w.width(), qMax(0, bottom - top));
}

QRect CalendarWidget::footerRect() const
{
    return QRect(kPad, height() - kPad - kFooterH, width() - 2 * kPad, kFooterH);
}

QRect CalendarWidget::cellRect(int index) const
{
    const QRect g = gridRect();
    if (g.isEmpty())
        return {};
    const int col = index % 7;
    const int row = index / 7;
    const qreal cw = g.width() / 7.0;
    const qreal ch = g.height() / 6.0;
    return QRectF(g.left() + col * cw, g.top() + row * ch, cw, ch).toAlignedRect();
}

QRect CalendarWidget::prevButtonRect() const
{
    const QRect h = headerRect();
    return QRect(h.left(), h.top(), kArrowW, h.height());
}

QRect CalendarWidget::nextButtonRect() const
{
    const QRect h = headerRect();
    return QRect(h.right() - kArrowW + 1, h.top(), kArrowW, h.height());
}

QRect CalendarWidget::todayButtonRect() const
{
    const QRect f = footerRect();
    const int btnH = 34;
    return QRect(f.right() - kTodayBtnW + 1, f.top() + (f.height() - btnH) / 2,
                 kTodayBtnW, btnH);
}

// ---------------------------------------------------------------------------
// Hit-testing
// ---------------------------------------------------------------------------

CalendarWidget::Region CalendarWidget::regionAt(const QPoint &pos) const
{
    if (prevButtonRect().contains(pos))
        return Region::Prev;
    if (nextButtonRect().contains(pos))
        return Region::Next;
    if (todayButtonRect().contains(pos))
        return Region::Today;
    if (gridRect().contains(pos))
        return Region::Grid;
    return Region::None;
}

int CalendarWidget::hitCell(const QPoint &pos) const
{
    const QRect g = gridRect();
    if (!g.contains(pos))
        return -1;
    const qreal cw = g.width() / 7.0;
    const qreal ch = g.height() / 6.0;
    const int col = int((pos.x() - g.left()) / cw);
    const int row = int((pos.y() - g.top()) / ch);
    const int index = row * 7 + col;
    return (index >= 0 && index < 42) ? index : -1;
}

void CalendarWidget::updateHover(const QPoint &pos)
{
    const Region region = regionAt(pos);
    const int cell = (region == Region::Grid) ? hitCell(pos) : -1;
    if (region != m_hover || cell != m_hoverCell) {
        m_hover = region;
        m_hoverCell = cell;
        update();
    }
    const bool interactive = region == Region::Prev || region == Region::Next
                             || region == Region::Today || cell >= 0;
    setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void CalendarWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    switch (regionAt(event->pos())) {
    case Region::Prev:
        shiftMonth(-1);
        break;
    case Region::Next:
        shiftMonth(1);
        break;
    case Region::Today:
        goToToday();
        break;
    case Region::Grid: {
        const int cell = hitCell(event->pos());
        if (cell < 0)
            return;
        const QDate day = m_month.addDays(cell - (m_month.dayOfWeek() - 1));
        m_selected = day;
        // Clicking an adjacent-month day navigates to that month, so the picked
        // day ends up visible in its own grid.
        if (day.month() != m_month.month())
            setMonth(day);
        update();
        break;
    }
    case Region::None:
        break;
    }
}

void CalendarWidget::mouseMoveEvent(QMouseEvent *event)
{
    updateHover(event->pos());
}

void CalendarWidget::leaveEvent(QEvent *)
{
    m_hover = Region::None;
    m_hoverCell = -1;
    update();
}

void CalendarWidget::wheelEvent(QWheelEvent *event)
{
    shiftMonth(event->angleDelta().y() > 0 ? -1 : 1);
    event->accept();
}

void CalendarWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Left:
        moveSelectionBy(-1);
        break;
    case Qt::Key_Right:
        moveSelectionBy(1);
        break;
    case Qt::Key_Up:
        moveSelectionBy(-7);
        break;
    case Qt::Key_Down:
        moveSelectionBy(7);
        break;
    case Qt::Key_PageUp:
        shiftMonth(-1);
        break;
    case Qt::Key_PageDown:
        shiftMonth(1);
        break;
    case Qt::Key_Home:
        goToToday();
        break;
    case Qt::Key_Escape:
        close();
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

// Moves the selection by @a days; crossing a month boundary follows the date,
// which is what makes Left/Right/Up/Down feel continuous across the grid.
void CalendarWidget::moveSelectionBy(int days)
{
    const QDate next = m_selected.addDays(days);
    m_selected = next;
    if (next.month() != m_month.month())
        setMonth(next);
    update();
}

// Navigates one month and moves the selection to the same day-of-month, so the
// footer always describes the visible grid.
void CalendarWidget::shiftMonth(int delta)
{
    QDate target(m_month.year(), m_month.month(), 1);
    target = target.addMonths(delta);
    const int day = qMin(m_selected.day(), target.daysInMonth());
    m_selected = QDate(target.year(), target.month(), day);
    setMonth(target);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

QString CalendarWidget::windowTitleFor(const QDate &date) const
{
    QString name = QLocale().monthName(date.month(), QLocale::LongFormat);
    if (!name.isEmpty())
        name[0] = name.at(0).toUpper();
    return QStringLiteral("%1 %2").arg(name).arg(date.year());
}

void CalendarWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor text = palette().color(QPalette::WindowText);
    const QColor accent = palette().color(QPalette::Highlight);

    // ---- header: prev/next arrows + big month title ----
    QFont arrowFont = font();
    arrowFont.setPixelSize(qRound(kHeaderH * 0.40));
    arrowFont.setBold(false);

    auto drawArrow = [&](const QRect &rect, bool hovered, const QString &glyph) {
        if (hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), kHoverAlpha));
            p.drawRoundedRect(rect, 8, 8);
        }
        p.setFont(arrowFont);
        p.setPen(hovered ? accent : text);
        p.drawText(rect, Qt::AlignCenter, glyph);
    };
    drawArrow(prevButtonRect(), m_hover == Region::Prev, QStringLiteral("\u2039"));
    drawArrow(nextButtonRect(), m_hover == Region::Next, QStringLiteral("\u203A"));

    QFont titleFont = font();
    titleFont.setPixelSize(qRound(kHeaderH * 0.36));
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(text);
    p.drawText(headerRect().adjusted(kArrowW, 0, -kArrowW, 0),
               Qt::AlignCenter, windowTitleFor(m_month));

    QColor sep = text;
    sep.setAlpha(40);
    p.setPen(sep);
    p.drawLine(weekdayRect().left(), weekdayRect().top() - 1,
               weekdayRect().right(), weekdayRect().top() - 1);

    // ---- weekday row, Monday first ----
    QFont wdFont = font();
    wdFont.setPixelSize(12);
    wdFont.setWeight(QFont::Medium);
    p.setFont(wdFont);
    QColor wd = text;
    wd.setAlpha(150);
    p.setPen(wd);
    const QRect grid = gridRect();
    const qreal cw = grid.width() / 7.0;
    for (int col = 0; col < 7; ++col) {
        const QRect colRect =
            QRectF(grid.left() + col * cw, weekdayRect().top(), cw, weekdayRect().height())
                .toAlignedRect();
        p.drawText(colRect, Qt::AlignCenter,
                   QLocale().dayName(col + 1, QLocale::ShortFormat));
    }

    // ---- day grid: one 7x6 cell per day, big numbers ----
    const QDate first(m_month.year(), m_month.month(), 1);
    const int offset = first.dayOfWeek() - 1; // Monday = 0
    const int daysInMonth = first.daysInMonth();

    for (int index = 0; index < 42; ++index) {
        const QDate day = first.addDays(index - offset);
        const bool inCurrent = index >= offset && index < offset + daysInMonth;
        const QRect cell = cellRect(index);
        if (cell.isEmpty())
            continue;

        const bool isToday = day == m_today;
        const bool isSelected = day == m_selected;
        const bool isHovered = index == m_hoverCell;
        const bool isWeekend = day.dayOfWeek() == 6 || day.dayOfWeek() == 7;

        if (isToday) {
            // Filled accent pill, like KDE's highlight for today.
            p.setPen(Qt::NoPen);
            p.setBrush(accent);
            const QRectF pill = QRectF(cell).adjusted(cell.width() * 0.14,
                                                      cell.height() * 0.14,
                                                      -cell.width() * 0.14,
                                                      -cell.height() * 0.14);
            p.drawRoundedRect(pill, pill.height() / 2.0, pill.height() / 2.0);
        } else if (isHovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), kHoverAlpha));
            p.drawRoundedRect(cell, 8, 8);
        } else if (isSelected) {
            p.setPen(QPen(accent, 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(cell.adjusted(1, 1, -1, -1), 8, 8);
        }

        QColor num = text;
        if (!inCurrent)
            num.setAlpha(60);
        else if (isWeekend)
            num.setAlpha(150);
        if (isToday)
            num = Qt::white;
        else if (isSelected)
            num = accent;

        QFont dayFont = font();
        dayFont.setPixelSize(qRound(qMin(cell.width(), cell.height()) * 0.40));
        dayFont.setBold(isToday || isSelected);
        p.setFont(dayFont);
        p.setPen(num);
        p.drawText(cell, Qt::AlignCenter, QString::number(day.day()));
    }

    // ---- footer: selected day's long date + "Hoy" button ----
    QColor footSep = text;
    footSep.setAlpha(40);
    p.setPen(footSep);
    p.drawLine(footerRect().left(), footerRect().top() - 1,
               footerRect().right(), footerRect().top() - 1);

    QFont footFont = font();
    footFont.setPixelSize(13);
    p.setFont(footFont);
    QColor footText = text;
    footText.setAlpha(170);
    p.setPen(footText);
    p.drawText(footerRect().adjusted(0, 0, -kTodayBtnW - 12, 0),
               Qt::AlignVCenter | Qt::AlignLeft,
               QLocale().toString(m_selected, QLocale::LongFormat));

    const QRect todayBtn = todayButtonRect();
    const bool todayHover = m_hover == Region::Today;
    if (todayHover) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), kHoverAlpha));
    } else {
        p.setPen(QPen(accent, 1));
        p.setBrush(Qt::NoBrush);
    }
    p.drawRoundedRect(todayBtn, todayBtn.height() / 2.0, todayBtn.height() / 2.0);
    p.setPen(accent);
    p.drawText(todayBtn, Qt::AlignCenter, QStringLiteral("Hoy"));
}
