#pragma once

#include <QDate>
#include <QWidget>

class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

// Standalone month calendar styled after the KDE digital-clock popup: big day
// numbers in a wide Monday-first grid, today filled with the accent color.
//
// Everything is painted by hand in one paintEvent — no child widgets — so the
// whole look (number size, grid spacing, highlights) lives in one place and
// scales with the window instead of fighting a layout engine. Colors come from
// the system palette, so the calendar follows light/dark themes and the current
// KDE accent automatically.
class CalendarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CalendarWidget(QWidget *parent = nullptr);

    QDate month() const { return m_month; }

public slots:
    // Shows the month containing @a firstOfMonth. The selection keeps its
    // day-of-month (clamped) so the footer always describes the visible month.
    void setMonth(const QDate &firstOfMonth);
    void goToToday();

signals:
    void monthChanged(const QDate &month);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    enum class Region { None, Prev, Next, Today, Grid };

    QRect headerRect() const;
    QRect weekdayRect() const;
    QRect gridRect() const;
    QRect footerRect() const;
    QRect cellRect(int index) const;
    QRect prevButtonRect() const;
    QRect nextButtonRect() const;
    QRect todayButtonRect() const;

    Region regionAt(const QPoint &pos) const;
    int hitCell(const QPoint &pos) const;
    void updateHover(const QPoint &pos);
    void shiftMonth(int delta);
    void moveSelectionBy(int days);
    QString windowTitleFor(const QDate &date) const;

    QDate m_month;          // first day of the displayed month
    QDate m_today;          // cached so a single process never jumps at midnight
    QDate m_selected;       // day described in the footer (starts at today)
    Region m_hover = Region::None;
    int m_hoverCell = -1;   // grid cell under the mouse, -1 when outside
};
