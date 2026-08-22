// The keyboard layout of a Wayland session, which under LXQt nobody owns.
//
// The problem it solves: `/etc/default/keyboard` (XKBLAYOUT=latam) is the X11
// answer, and lxqt-config-input applies it with setxkbmap — also X11. Under
// Wayland the compositor owns the keymap, and this session's compositor is
// KWin, which builds it from the `[Layout]` group of `kxkbrc` and ignores both
// of the above. So a session configured entirely through LXQt still typed with
// whatever kxkbrc happened to say (here: `es`, i.e. Spain, on a machine whose
// every other file said `latam`), and nothing in the LXQt control centre could
// move it.
//
// The fix is to write the file KWin reads and tell it to re-read it. Two
// details, both measured on this session (2026-08-22) and neither obvious:
//
//   - **`org.kde.KWin.reconfigure` does NOT reload the keymap.** That method
//     reparses kwinrc; kxkbrc is watched by a KConfigWatcher instead. Writing
//     the file and calling reconfigure leaves KWin on the old layout, which
//     reads exactly like "the write did not work".
//   - **What KWin listens to is the KConfig change notification**: the D-Bus
//     signal `org.kde.kconfig.notify.ConfigChanged` on the object path
//     `/kxkbrc`, whose argument is a map of group name -> changed keys. Emitting
//     it by hand switched the live session from `es` to `latam` with no
//     restart. `kwriteconfig6 --notify` emits the same signal, but only when
//     the value it wrote actually differed — which is why this class emits it
//     itself instead: it wants one notification for a batch of keys, and it
//     wants the "Aplicar ahora" button to be able to force a reload when KWin
//     and the file have drifted apart.
//
// Off by default and inert while off, for the same reason as QtCompat: kxkbrc
// lives in XDG_CONFIG_HOME and the notification goes to the session bus, so a
// test harness that isolated only XDG_DATA_HOME must not be able to change the
// developer's keyboard just by constructing this.

#pragma once

#include <QList>
#include <QObject>
#include <QString>

class KeyboardControl : public QObject
{
    Q_OBJECT

public:
    explicit KeyboardControl(QObject *parent = nullptr);

    // ---- persisted settings (shared kdock.conf, group [Keyboard]) ----------
    // App-wide, never per dock: there is one keymap per session.
    //
    // Turning the master switch off only stops applying; kxkbrc keeps whatever
    // was last written, for the user to manage with the KDE control module (or
    // by hand). Restoring would mean remembering a "before" that anything else
    // in the session may have changed in the meantime.
    static bool enabled();
    void setEnabled(bool on);

    // The four xkb names. Empty means different things per key, and the
    // difference matters:
    //
    //   - `layout` empty = nothing is applied at all. It is the whole point of
    //     the feature, so with no layout there is no batch to write.
    //   - `variant` empty is a *value* ("the plain layout") and is written as
    //     an empty VariantList, which is what clears a stale variant.
    //   - `model` and `options` empty mean "let KWin decide", so the key is
    //     removed from kxkbrc rather than blanked — a blank Options is not the
    //     same as no Options once xkb has seen one.
    static QString layout();
    void setLayout(const QString &id);
    static QString variant();
    void setVariant(const QString &id);
    static QString model();
    void setModel(const QString &id);
    static QString options();
    void setOptions(const QString &value);

    // Write the keys that differ and tell KWin to re-read. No-op while off.
    // `force` sends the notification even when the file already matched, which
    // is what the tab's button needs: the interesting failure is KWin and
    // kxkbrc disagreeing, and then there is nothing to write.
    void apply(bool force = false);
    void applyNow() { apply(true); }

    // ---- what is actually in effect ---------------------------------------
    // The `[Layout]` group as it is on disk right now. Reading is always safe
    // (kxkbrc is a plain INI), so the tab can show it with the feature off.
    static QString configuredLayout();
    static QString configuredVariant();
    static QString configuredModel();
    static QString configuredOptions();
    static QString kxkbrcPath();

    // What KWin reports over org.kde.KeyboardLayouts. Refreshed asynchronously
    // (a blocking call here would be one more way to freeze the dock's startup,
    // see the D-Bus deadlock of 2026-08-21), so this returns the last answer
    // and `refreshActive()` asks for a new one.
    struct ActiveLayout
    {
        QString name;        // "latam"
        QString variant;     // ""
        QString displayName; // "Spanish (Latin American)"
    };
    QList<ActiveLayout> activeLayouts() const { return m_active; }
    bool kwinAnswered() const { return m_answered; }
    void refreshActive();

    // ---- the xkb catalogue -------------------------------------------------
    // Parsed from the rules list (`evdev.lst`), which is the same file the KDE
    // and GNOME control panels read. `KDOCK_TEST_XKB_RULES` overrides the path,
    // so the parser is testable without depending on the host's xkb-data.
    struct Entry
    {
        QString id;
        QString name;
    };
    static QList<Entry> availableLayouts();
    // Variants of one layout, without the "(plain)" entry — the tab adds that
    // one itself, because "no variant" is not a row of the rules file.
    static QList<Entry> availableVariants(const QString &layout);
    static QList<Entry> availableModels();
    static QString rulesPath();

    // The X11 answer, from /etc/default/keyboard: what the session *would* use
    // if it were X11, and in practice what the user believes is configured. The
    // tab offers it as the starting value, which is the only reason this class
    // knows about a file KWin never reads.
    static QString systemLayout();
    static QString systemVariant();
    static QString systemModel();

signals:
    void changed();
    // A fresh answer from KWin arrived (or failed to).
    void activeChanged();

private:
    // One kxkbrc key and what should become of it.
    struct Pending
    {
        QString key;
        QString value;
        bool remove = false;
    };
    QList<Pending> pendingWrites() const;
    // Emit org.kde.kconfig.notify.ConfigChanged for `[Layout]` and the given
    // keys. This is the half that makes KWin act.
    static void notifyKWin(const QStringList &keys);

    QList<ActiveLayout> m_active;
    bool m_answered = false;
};
