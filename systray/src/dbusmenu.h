// com.canonical.dbusmenu client.
//
// StatusNotifierItem does not carry its context menu: the item exposes a `Menu`
// object path and the *host* is expected to fetch the menu over DBusMenu and
// draw it. Asking the item to draw it itself (SNI's ContextMenu method) cannot
// work here — on Wayland an app with no visible window has no surface to parent
// an xdg_popup to, and plenty of items (blueman) do not implement the method at
// all.

#pragma once

#include <QDBusArgument>
#include <QDBusMessage>
#include <QObject>
#include <QPixmap>
#include <QVariantMap>

// One node of the menu as it comes off the wire: `(ia{sv}av)`, where the last
// field is the children — each an `av` variant wrapping another such struct.
struct DBusMenuLayoutItem
{
    int id = 0;
    QVariantMap properties;
    QList<DBusMenuLayoutItem> children;
};
Q_DECLARE_METATYPE(DBusMenuLayoutItem)

// Recursive (de)marshalling. MUST be paired with qDBusRegisterMetaType (done in
// the DBusMenuClient ctor): without the type registered, QtDBus hands
// back an empty value and manual demarshalling of the nested struct desyncs
// until libdbus aborts the process ("type struct not a basic type") — the same
// trap the SNI pixmap types fell into.
QDBusArgument &operator<<(QDBusArgument &arg, const DBusMenuLayoutItem &item);
const QDBusArgument &operator>>(const QDBusArgument &arg, DBusMenuLayoutItem &item);

class DBusMenuClient : public QObject
{
    Q_OBJECT
public:
    DBusMenuClient(const QString &service, const QString &path, QObject *parent = nullptr);
    ~DBusMenuClient() override;

    // Asks for the whole tree. Always asynchronous: a blocking call against an
    // unresponsive app would freeze the dock for the D-Bus default timeout.
    void requestLayout();
    bool ready() const { return m_ready; }

    // The tree flattened for QML: a list of maps with `children` nested, see
    // nodeToVariant(). Empty until layoutReady().
    QVariantList tree() const;

    // A menu entry was chosen.
    void trigger(int id);
    // Items are allowed to populate a submenu only when told it is about to be
    // shown; a true reply means the layout changed and is re-requested.
    void aboutToShow(int id);
    // The spec's menu lifecycle events. Some items build their menu on "opened"
    // rather than on AboutToShow.
    void setOpen(bool open);

    // Icon of a node that ships raw image data (`icon-data`, a PNG blob) rather
    // than a theme icon name. Null if the node has none.
    QPixmap iconData(int id) const;

signals:
    void layoutReady();
    void layoutFailed();
    // The item changed its menu (LayoutUpdated / ItemsPropertiesUpdated) — an
    // open menu should refresh.
    void layoutInvalidated();

private slots:
    // String-based SLOT() connections, so these must be slots.
    void onLayoutUpdated();
    void onItemsPropertiesUpdated();

private:
    void sendEvent(int id, const QString &eventId);
    // Every call goes out through this: a QDBusMessage on the session bus, never
    // a QDBusInterface. The convenience class fetches the introspection XML in
    // its *constructor*, with a blocking call — and this object is built while
    // handling the item's own (blocking) registration, so that read deadlocks
    // both processes until the bus times out. See the note in systray.h.
    QDBusMessage menuCall(const QString &member, const QVariantList &args) const;
    const DBusMenuLayoutItem *findNode(const DBusMenuLayoutItem &node, int id) const;
    QVariantList childrenToVariant(const DBusMenuLayoutItem &node) const;
    QVariantMap nodeToVariant(const DBusMenuLayoutItem &node) const;

    QString m_service;
    QString m_path;
    DBusMenuLayoutItem m_root;
    bool m_ready = false;
    bool m_pending = false;
    // Bumped on every layout that arrives, so the QML Image URLs of icon-data
    // entries change and QML does not serve a stale pixmap from its cache.
    int m_serial = 0;
};
