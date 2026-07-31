// A QTabBar that paints a per-tab background tint, so the settings dialog's
// ten tabs are told apart by color instead of by reading their labels. The
// colors come from the dock's own app icons (see SettingsDialog::tabPalette),
// not from anything computed here: this class only knows how to paint them.

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
