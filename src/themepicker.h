// Qt Widgets twin of qml/ThemeListPopup.qml: the one theme selector the whole
// Settings dialog uses. Same layout as the dock widget's popup - search field
// on top, one row per entry with a preview, the chosen one ticked, and a
// favourites checkbox - so the dialog and the dock feel like the same control.
//
// It comes in two modes. ApplyToDesktop applies to KDE right away (Colores →
// "· Apply icon theme" / "· Apply color scheme"); PickValue only reports the id
// and lets the caller store it, which is what every other selector needs - the
// dock's own icon-theme override, the widget icon sets, and DarkMode's six.
//
// Both lists are long (183 icon themes / 456 color schemes on a full install)
// and one icon preview costs ~4 ms to resolve, so the rows are built lazily:
// only the ones the viewport actually shows get their widget (see
// ThemePickerPopup::buildVisibleRows).

#pragma once

#include <QFrame>
#include <QPushButton>
#include <QString>
#include <QVariantMap>

class AppearanceControl;
class QCheckBox;
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
    // What choosing a row does.
    enum Mode {
        ApplyToDesktop, // straight to KDE (plasma-changeicons / -colorscheme)
        PickValue,      // only report it; the caller stores it
    };

    // kind: "icons" | "colors", the same token AppearanceControl takes.
    ThemePickerPopup(AppearanceControl *appearance, const QString &kind, Mode mode = ApplyToDesktop,
                     QWidget *parent = nullptr);

    // Show under (or above, if there is no room) the given widget.
    void showFor(QWidget *anchor);

    // In PickValue mode: which id is ticked, and the label of the leading
    // do-nothing row (empty id) - "(System default)", "(no cambiar)"…
    void setSelectedId(const QString &id) { m_selectedId = id; }
    void setSpecialEntry(const QString &label) { m_specialLabel = label; }

signals:
    // PickValue only. ApplyToDesktop applies and stays quiet.
    void chosen(const QString &id);

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
    Mode m_mode = ApplyToDesktop;
    QString m_selectedId;   // PickValue: what carries the tick
    QString m_specialLabel; // PickValue: leading row with an empty id

    QLineEdit *m_search = nullptr;
    QLabel *m_count = nullptr;
    QCheckBox *m_keepOpen = nullptr;
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
    using Mode = ThemePickerPopup::Mode;

    ThemePickerButton(AppearanceControl *appearance, const QString &kind,
                      Mode mode = ThemePickerPopup::ApplyToDesktop, QWidget *parent = nullptr);

    // PickValue only. setCurrentId() is silent by contract: it is what the
    // synced copy in the other tab calls, so emitting would ping-pong.
    QString currentId() const { return m_currentId; }
    void setCurrentId(const QString &id);
    void setSpecialEntry(const QString &label);

    QSize sizeHint() const override;

signals:
    void picked(const QString &id);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void refresh();

    AppearanceControl *m_appearance = nullptr;
    QString m_kind;
    bool m_iconsMode = true;
    Mode m_mode = ThemePickerPopup::ApplyToDesktop;
    QString m_currentId; // PickValue: the whole state
    QString m_specialLabel;
    QString m_currentName;
    QVariantMap m_previewEntry; // resolved into m_preview on the first paint
    QPixmap m_preview;
    ThemePickerPopup *m_popup = nullptr;
};

// Preview strip of an entry, 22 px tall: three sample icons rendered in that
// icon theme, or three swatches of the scheme's own colors. Shared by the
// button and the rows. Cached, so scrolling back over a row is free.
QPixmap themePreviewPixmap(bool iconsMode, const QVariantMap &entry, qreal dpr);
