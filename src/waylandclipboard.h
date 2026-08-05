// Clipboard access that does not need keyboard focus, via the Wayland protocol
// ext-data-control-v1 (the standardized successor of wlr-data-control; KWin and
// KDE's own KSystemClipboard both moved to it).
//
// Why this exists: on Wayland a client may only read wl_data_device while it
// holds keyboard focus, and the dock is a keyboard-inert layer surface. Passive
// clipboard capture through QClipboard therefore only worked while a popup had
// grabbed the keyboard. data-control is the protocol clipboard managers use and
// is focus-independent, in both directions (reading the selection and owning it).
//
// Note: unlike org_kde_plasma_window_management, this interface is NOT on KWin's
// restricted list, so nothing has to be declared in kdock.desktop and ksycoca is
// not involved.
//
// If the compositor does not advertise the global (X11, Xvfb, older KWin),
// active() stays false and ClipboardHistory keeps using QClipboard.

#pragma once

#include <QByteArray>
#include <QObject>
#include <QStringList>

#include <QtWaylandClient/QWaylandClientExtension>

#include "qwayland-ext-data-control-v1.h"

// One clipboard offer: what some other client currently has on the selection,
// as a list of mime types plus a way to ask for the bytes.
class DataControlOffer : public QObject, public QtWayland::ext_data_control_offer_v1
{
    Q_OBJECT
public:
    DataControlOffer(struct ::ext_data_control_offer_v1 *object, QObject *parent);
    ~DataControlOffer() override;

    QStringList mimeTypes() const { return m_mimeTypes; }

protected:
    void ext_data_control_offer_v1_offer(const QString &mimeType) override;

private:
    QStringList m_mimeTypes;
};

// Our side of the clipboard: holds the bytes and serves them to whoever pastes.
class DataControlSource : public QObject, public QtWayland::ext_data_control_source_v1
{
    Q_OBJECT
public:
    DataControlSource(struct ::ext_data_control_source_v1 *object, const QByteArray &data,
                      QObject *parent);

signals:
    // The compositor gave the selection to somebody else; this source is dead.
    void cancelled();

protected:
    void ext_data_control_source_v1_send(const QString &mimeType, int32_t fd) override;
    void ext_data_control_source_v1_cancelled() override;

private:
    QByteArray m_data;
};

class DataControlDevice : public QObject, public QtWayland::ext_data_control_device_v1
{
    Q_OBJECT
public:
    DataControlDevice(struct ::ext_data_control_device_v1 *object, QObject *parent);
    ~DataControlDevice() override;

signals:
    // Null when the selection was cleared.
    void selectionOffered(DataControlOffer *offer);

protected:
    void ext_data_control_device_v1_data_offer(struct ::ext_data_control_offer_v1 *id) override;
    void ext_data_control_device_v1_selection(struct ::ext_data_control_offer_v1 *id) override;
    void ext_data_control_device_v1_finished() override;

private:
    DataControlOffer *m_currentOffer = nullptr;
};

class WaylandClipboard : public QWaylandClientExtensionTemplate<WaylandClipboard>,
                         public QtWayland::ext_data_control_manager_v1
{
    Q_OBJECT
public:
    explicit WaylandClipboard(QObject *parent = nullptr);

    // True once the compositor advertised the global and the seat's device is
    // bound. Everything else is a no-op until then.
    bool active() const { return m_device != nullptr; }

    // Take ownership of the clipboard. The bytes are served asynchronously to
    // any client that pastes, for as long as we own the selection.
    void setText(const QString &text);
    void setImage(const QByteArray &pngData);

signals:
    void textCopied(const QString &text);
    void imageCopied(const QByteArray &imageData, const QString &mimeType);

private:
    void ensureDevice();
    void onSelection(DataControlOffer *offer);
    void takeSelection(const QByteArray &data, const QStringList &mimeTypes);

    DataControlDevice *m_device = nullptr;
    // Our own source while we own the clipboard. The compositor echoes our
    // selection back at us; reading it would mean reading from our own process,
    // so while this is alive the incoming selection is ignored.
    DataControlSource *m_source = nullptr;
};
