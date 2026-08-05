// A QTabBar that paints a per-tab background tint, so the settings dialog's
// dozen tabs are told apart by color instead of by reading their labels. The
// colors come from the dock's own app icons (see SettingsDialog::tabPalette),
// not from anything computed here: this class only knows how to paint them.
//
// The bar is used in QTabWidget::West (a column down the left side). Qt's own
// painting rotates the label 90 degrees there, which would make the column ask
// for as much height as the horizontal bar asked for width (~1100 px for twelve
// tabs) and drop it into scroll-arrow mode again. So in a vertical shape this
// class draws the label itself, horizontally: one tab then costs ~30 px of
// height instead of ~90 px of width, and around 28 fit before Qt's scroll
// arrows appear.

#pragma once

#include <QColor>
#include <QList>
#include <QTabBar>
#include <QTabWidget>

class ColoredTabBar : public QTabBar
{
    Q_OBJECT
public:
    using QTabBar::QTabBar;

    // Assigns the tint of one tab. Colors are stored fully opaque; the alpha
    // used for the fill depends on the tab's state and is applied on paint.
    void setTabColor(int index, const QColor &color);
    void clearTabColors();

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize tabSizeHint(int index) const override;

private:
    // True for the West/East shapes, i.e. the tabs stack in a column.
    bool isVertical() const;
    // Width the column needs: the widest label plus padding. Uniform across
    // tabs so the column has a straight right edge.
    int columnWidth() const;

    QList<QColor> m_colors;
};

// QTabWidget::setTabBar() is protected, so swapping in ColoredTabBar needs a
// subclass. Nothing else is customized here.
class ColoredTabWidget : public QTabWidget
{
    Q_OBJECT
public:
    explicit ColoredTabWidget(QWidget *parent = nullptr);

    ColoredTabBar *coloredTabBar() const;
};
