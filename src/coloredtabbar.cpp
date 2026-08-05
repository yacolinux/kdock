#include "coloredtabbar.h"

#include <QPainter>
#include <QStyleOptionTab>
#include <QStylePainter>

namespace {

// Linear mix of two opaque colors. Used instead of painting the tint with an
// alpha so the result is a solid color we can measure the luminance of (for the
// label color) without guessing what is behind the tab bar.
QColor mix(const QColor &base, const QColor &tint, qreal f)
{
    const qreal g = 1.0 - f;
    return QColor(int(base.red() * g + tint.red() * f),
                  int(base.green() * g + tint.green() * f),
                  int(base.blue() * g + tint.blue() * f));
}

// Black or white, whichever stays readable on top of a given fill. The dialog
// follows the KDE color scheme (see Theme::applyAppPalette), so the fills can
// land anywhere between very light and very dark.
QColor labelColor(const QColor &fill)
{
    const double lum = 0.299 * fill.red() + 0.587 * fill.green() + 0.114 * fill.blue();
    return lum < 140 ? QColor(255, 255, 255) : QColor(20, 20, 20);
}

} // namespace

void ColoredTabBar::setTabColor(int index, const QColor &color)
{
    if (index < 0)
        return;
    while (m_colors.size() <= index)
        m_colors.append(QColor());
    m_colors[index] = color;
    update();
}

void ColoredTabBar::clearTabColors()
{
    m_colors.clear();
    update();
}

bool ColoredTabBar::isVertical() const
{
    const QTabBar::Shape s = shape();
    return s == QTabBar::RoundedWest || s == QTabBar::RoundedEast
           || s == QTabBar::TriangularWest || s == QTabBar::TriangularEast;
}

int ColoredTabBar::columnWidth() const
{
    const QFontMetrics fm = fontMetrics();
    int w = 0;
    for (int i = 0; i < count(); ++i)
        w = qMax(w, fm.horizontalAdvance(tabText(i)));
    return w + 28; // left indent + right breathing room
}

QSize ColoredTabBar::tabSizeHint(int index) const
{
    if (isVertical()) {
        // Every tab gets the same width so the column has a straight edge, and
        // a fixed row height: the label is drawn horizontally (see paintEvent),
        // so its length costs nothing vertically.
        Q_UNUSED(index);
        return QSize(columnWidth(), fontMetrics().height() + 14);
    }
    // The tint reads as a block rather than as a stripe behind the text, so the
    // tabs get a little breathing room.
    QSize s = QTabBar::tabSizeHint(index);
    s.rwidth() += 4;
    s.rheight() += 4;
    return s;
}

void ColoredTabBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QStylePainter p(this);
    const QColor base = palette().color(QPalette::Window);
    const bool vertical = isVertical();

    for (int i = 0; i < count(); ++i) {
        QStyleOptionTab opt;
        initStyleOption(&opt, i);

        const QColor tint = i < m_colors.size() ? m_colors.at(i) : QColor();
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hovered = opt.state & QStyle::State_MouseOver;

        if (!tint.isValid() && !vertical) {
            p.drawControl(QStyle::CE_TabBarTab, opt); // untinted: plain style tab
            continue;
        }

        // A vertical bar with no tint still cannot fall back to the style: it
        // would rotate the label. Paint the plain window color instead.
        const QColor fill = tint.isValid()
                                ? mix(base, tint, selected ? 0.80 : (hovered ? 0.45 : 0.20))
                                : (selected ? palette().color(QPalette::Highlight)
                                            : (hovered ? mix(base, base.lighter(140), 0.5) : base));

        // Unselected tabs are inset a little more so the current one stands out
        // by shape too, not only by a stronger tint. Horizontally that means
        // "sits lower"; in the column it means "a touch narrower".
        const QRect r = vertical ? tabRect(i).adjusted(selected ? 1 : 4, 1, -1, -1)
                                 : tabRect(i).adjusted(1, selected ? 1 : 4, -1, 0);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(r, 4, 4);
        // Full-strength accent along the leading edge of the current tab: at
        // 80% mix the selected fill is still tied to the scheme's background,
        // and on a dark scheme that alone reads as "slightly different gray".
        // The edge away from the page frame is used (top / left).
        if (selected && tint.isValid()) {
            p.setBrush(tint);
            if (vertical)
                p.drawRoundedRect(QRect(r.left(), r.top() + 2, 5, r.height() - 4), 2, 2);
            else
                p.drawRoundedRect(QRect(r.left() + 2, r.top(), r.width() - 4, 5), 2, 2);
        }
        p.setRenderHint(QPainter::Antialiasing, false);

        const QColor text = labelColor(fill);
        if (vertical) {
            // Drawn by hand: CE_TabBarTabLabel rotates the text 90 degrees for
            // the West/East shapes, which is what this class exists to avoid.
            p.setPen(text);
            p.drawText(r.adjusted(12, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       tabText(i));
            continue;
        }
        // Only the label: drawing CE_TabBarTabShape would paint the style's own
        // background over the tint.
        opt.palette.setColor(QPalette::WindowText, text);
        opt.palette.setColor(QPalette::ButtonText, text);
        opt.palette.setColor(QPalette::Text, text);
        p.drawControl(QStyle::CE_TabBarTabLabel, opt);
    }
}

ColoredTabWidget::ColoredTabWidget(QWidget *parent)
    : QTabWidget(parent)
{
    setTabBar(new ColoredTabBar(this));
    // Tabs down the left side. The horizontal bar was the hard limit on how
    // many tabs the dialog could have (eleven titles already asked for
    // 1086 px); a column trades that for ~30 px of height each.
    setTabPosition(QTabWidget::West);
    tabBar()->setElideMode(Qt::ElideNone);
    // Qt's default for a West bar is centred; with a dozen tabs the column
    // reads better anchored to the top of the dialog.
    tabBar()->setExpanding(false);
}

ColoredTabBar *ColoredTabWidget::coloredTabBar() const
{
    return static_cast<ColoredTabBar *>(tabBar());
}
