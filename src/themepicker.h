// Qt Widgets twin of qml/ThemeListPopup.qml: the picker for a global KDE
// appearance setting, used twice in Settings → Colores (icon theme / color
// scheme). Same layout as the dock widget's popup - search field on top, one
// row per entry with a preview, the applied one ticked, and a favourites
// checkbox - so the dialog and the dock feel like the same control.
//
// Both lists are long (183 icon themes / 456 color schemes on a full install)
// and one icon preview costs ~4 ms to resolve, so the rows are built lazily:
// only the ones the viewport actually shows get their widget (see
// ThemePickerPopup::buildVisibleRows).

#pragma once

#include <QFrame>
#include <QPushButton>
#include <QString>
#include <QVariantList>

class AppearanceControl;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QTimer;

// One row of the popup. It reports its own clicks instead of letting them fall
// through to QListWidget::itemClicked: a row put in with setItemWidget() covers
// the item, and Qt::WA_TransparentForMouseEvents is not a way around it -
// that attribute drops mouse events for the widget *and its children*, so the
// favourites checkbox stopped responding (2026-08-05). The checkbox is a plain
// child here: it gets the click first and accepts it, and everything else
// (QLabel, images) ignores mouse events, so those clicks reach this widget.
class ThemeRowWidget : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;

    // Draws the divider that closes the favourites block above this row.
    void setSeparator(bool on) { m_separator = on; }

signals:
    void activated();

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    // Hover highlight and separator. The row widget covers the item, so the
    // list's own hover never shows - without this the popup looks inert.
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_hovered = false;
    bool m_separator = false;
};

// The popup itself. Qt::Popup (not a QMenu, which cannot host a usable
// QLineEdit): it grabs the mouse, closes on a click outside, and forwards keys
// to the focused child.
class ThemePickerPopup : public QFrame
{
    Q_OBJECT
public:
    // kind: "icons" | "colors", the same token AppearanceControl takes.
    ThemePickerPopup(AppearanceControl *appearance, const QString &kind, QWidget *parent = nullptr);

    // Show under (or above, if there is no room) the given widget.
    void showFor(QWidget *anchor);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    // Watches the list's viewport: its final size only arrives after the popup
    // is shown, and until then almost no row counts as visible.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void populate();
    // Give a row widget to every item the viewport shows and that has none yet.
    void buildVisibleRows();
    QWidget *makeRow(const QVariantMap &entry, bool firstNonFavorite);
    void applyEntry(const QVariantMap &entry);

    AppearanceControl *m_appearance = nullptr;
    QString m_kind;
    bool m_iconsMode = true;

    QLineEdit *m_search = nullptr;
    QLabel *m_count = nullptr;
    QListWidget *m_list = nullptr;
    QTimer *m_debounce = nullptr;

    // Entries currently listed, in display order.
    QVariantList m_entries;
    // Frozen while the popup is open: reordering under the cursor because the
    // user ticked a favourite would move the row they are pointing at.
    bool m_open = false;
};

// The button that stands in for the old QComboBox: preview + current name +
// a drop-down arrow, painted by hand so the name elides on the left-hand side
// of the arrow instead of centring like a QToolButton would.
class ThemePickerButton : public QPushButton
{
    Q_OBJECT
public:
    ThemePickerButton(AppearanceControl *appearance, const QString &kind,
                      QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void refresh();

    AppearanceControl *m_appearance = nullptr;
    QString m_kind;
    bool m_iconsMode = true;
    QString m_currentName;
    QPixmap m_preview;
    ThemePickerPopup *m_popup = nullptr;
};

// Preview strip of an entry, 22 px tall: three sample icons rendered in that
// icon theme, or three swatches of the scheme's own colors. Shared by the
// button and the rows. Cached, so scrolling back over a row is free.
QPixmap themePreviewPixmap(bool iconsMode, const QVariantMap &entry, qreal dpr);
