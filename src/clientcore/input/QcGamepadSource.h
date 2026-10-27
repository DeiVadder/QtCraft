#pragma once

#include "clientcore/input/QcGamepadState.h"

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

#include <optional>

// The gamepads the client sees, behind one interface (decision E-540): the SDL3 backend of the
// game (QcSdlGamepadSource in src/app/input, polled on a Qt timer) and the virtual pads of tests
// and the control interface (QcVirtualGamepadSource) look the same to the input mapping. The base
// keeps the pads and their last state; a backend reports changes through the protected functions,
// which emit the signals. Ids are positive and never reused by one source. Lives on one thread.
class QcGamepadSource : public QObject
{
    Q_OBJECT

public:
    explicit QcGamepadSource(QObject *parent = nullptr);
    ~QcGamepadSource() override;
    Q_DISABLE_COPY_MOVE(QcGamepadSource)

    // A short name of the backend for logs and debug screens ("sdl3", "virtual").
    virtual QString backendName() const = 0;
    // Whether the backend runs (SDL initialised); a source that is not available has no pads.
    virtual bool isAvailable() const = 0;
    // Reads the hardware now (a no-op for sources that are fed from outside).
    virtual void poll() {}

    // The connected pads in the order they were connected.
    QList<QcGamepadInfo> pads() const;
    bool contains(int id) const { return m_pads.contains(id); }
    // The pad's last state; all released and centred for an unknown id.
    QcGamepadState state(int id) const;
    // The connected pad `id`; nullopt for an unknown id.
    std::optional<QcGamepadInfo> info(int id) const;
    // Lets pad `id` vibrate with `strength` (0..1) for `durationMs` (decision E-615); false when it
    // cannot or the id is unknown. A new call replaces a running one.
    virtual bool rumble(int id, float strength, int durationMs);

signals:
    void padConnected(int id);
    void padDisconnected(int id);
    // The buttons or axes of a pad changed (raw values, before dead zone and curve).
    void stateChanged(int id);

protected:
    // Adds a pad with a released state and returns its new id (the id of `info` is ignored).
    int addPad(QcGamepadInfo info);
    void removePad(int id);
    // Stores the state and emits stateChanged when it differs from the last one.
    void updateState(int id, const QcGamepadState &state);

private:
    struct Pad
    {
        QcGamepadInfo info;
        QcGamepadState state;
    };

    QMap<int, Pad> m_pads;
    int m_nextId = 1;
};
