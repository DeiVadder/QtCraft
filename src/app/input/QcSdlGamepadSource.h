#pragma once

#include "clientcore/input/QcGamepadSource.h"

#include <QHash>
#include <QSet>
#include <QTimer>

struct SDL_Gamepad;

// The game's gamepads through SDL3 (decision E-540): only SDL's gamepad subsystem (with joystick,
// HIDAPI, haptic and sensor), no window and no event queue. A Qt timer calls poll(), which lets SDL
// update its devices (SDL_UpdateGamepads), compares SDL's list of pads with the open ones
// (hot-plug) and reads buttons and axes into normalised states. SDL's own signal handlers stay off.
// The timer runs every `activeIntervalMs` while a pad is open and every idleIntervalMs while none
// is (looking for new pads needs no 8 ms). Lives on the main thread, like SDL's gamepad subsystem
// (asserted). Only built with QC_GAMEPAD (the CMake option); create it through QcGamepadBackend.
class QcSdlGamepadSource : public QcGamepadSource
{
    Q_OBJECT

public:
    static constexpr int idleIntervalMs = 250;

    // Polls once constructed, when SDL starts (main thread only).
    explicit QcSdlGamepadSource(int activeIntervalMs = 8, QObject *parent = nullptr);
    ~QcSdlGamepadSource() override;
    Q_DISABLE_COPY_MOVE(QcSdlGamepadSource)

    QString backendName() const override;
    bool isAvailable() const override { return m_available; }
    void poll() override;
    bool rumble(int id, float strength, int durationMs) override;
    // SDL's message when it did not start, else empty.
    QString initError() const { return m_initError; }
    // The current interval of the poll timer (active or idle).
    int pollIntervalMs() const { return m_timer.interval(); }

private:
    struct OpenPad
    {
        int id = 0;
        SDL_Gamepad *gamepad = nullptr;
    };

    void openNewPads();
    void closeGonePads();
    QcGamepadState readState(SDL_Gamepad *gamepad) const;

    bool m_available = false;
    QString m_initError;
    int m_activeIntervalMs = 8;
    QTimer m_timer;
    // By SDL's joystick instance id.
    QHash<quint32, OpenPad> m_open;
    // Joystick instance ids SDL could not open: warned about once, tried again only after they
    // were gone from SDL's list.
    QSet<quint32> m_failed;
};
