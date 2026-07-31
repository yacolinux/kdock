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

QSize ColoredTabBar::tabSizeHint(int index) const
{
    // The tint reads as a block rather than as a stripe behind the text, so the
    // tabs get a little breathing room. Kept small on purpose: the dialog is
    // 1000px wide and the ten tabs already need ~940px, so a wider padding
    // pushes the bar into its scroll-arrow mode.
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

    for (int i = 0; i < count(); ++i) {
        QStyleOptionTab opt;
        initStyleOption(&opt, i);

        const QColor tint = i < m_colors.size() ? m_colors.at(i) : QColor();
        if (!tint.isValid()) {
            p.drawControl(QStyle::CE_TabBarTab, opt); // untinted: plain style tab
            continue;
        }

        const bool selected = opt.state & QStyle::State_Selected;
        const bool hovered = opt.state & QStyle::State_MouseOver;
        const QColor fill = mix(base, tint, selected ? 0.80 : (hovered ? 0.45 : 0.20));

        // Unselected tabs sit slightly lower and shorter so the current one
        // stands out by shape too, not only by a stronger tint.
        const QRect r = tabRect(i).adjusted(1, selected ? 1 : 4, -1, 0);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(r, 4, 4);
        // Full-strength accent along the top of the current tab: at 80% mix the
        // selected fill is still tied to the scheme's background, and on a dark
        // scheme that alone reads as "slightly different gray". The top edge is
        // used because the bottom one is where the tab meets the page frame.
        if (selected) {
            p.setBrush(tint);
            p.drawRoundedRect(QRect(r.left() + 2, r.top(), r.width() - 4, 5), 2, 2);
        }
        p.setRenderHint(QPainter::Antialiasing, false);

        // Only the label: drawing CE_TabBarTabShape would paint the style's own
        // background over the tint.
        const QColor text = labelColor(fill);
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
}

ColoredTabBar *ColoredTabWidget::coloredTabBar() const
{
    return static_cast<ColoredTabBar *>(tabBar());
}
