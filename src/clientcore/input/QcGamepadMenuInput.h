#pragma once

#include "clientcore/input/QcGamepadState.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <optional>

class QcGamepadSource;

// What a pad does on the screens (decision E-583), without a window: the d-pad or the left stick
// move the focus (held: repeated after repeatDelayMs every repeatIntervalMs), south confirms, east
// goes back, west and north are the secondary actions (in slot screens: one item, quick move),
// start, and the shoulders switch tabs; the right stick scrolls. Any pad counts. Commands come
// from the source's state changes and, for repeats and scrolling, from a timer that runs only
// while a direction is held or the right stick is pushed. The window side (QcMenuNavigator) acts
// on them; whether screens take them at all decides its owner through the gate: while it is closed
// (the world takes the pad) held directions and the scrolling stick are dropped, and once it opens
// they count again only after they went back to neutral, so a direction or a look held in the world
// does not run on in a screen that opens. Lives in one thread.
class QcGamepadMenuInput : public QObject
{
    Q_OBJECT

public:
    enum class Command { Up, Down, Left, Right, Confirm, Back, Secondary, Tertiary, Start, TabPrevious, TabNext };
    Q_ENUM(Command)

    static constexpr qint64 repeatDelayMs = 400;
    static constexpr qint64 repeatIntervalMs = 110;
    // A stick moves the focus beyond this deflection (raw) and scrolls beyond scrollDeadZone.
    static constexpr float stickDirection = 0.5f;
    static constexpr float scrollDeadZone = 0.25f;
    // Pixels per second of the right stick at full deflection.
    static constexpr float scrollSpeed = 1400.0f;
    static constexpr int timerIntervalMs = 16;

    // Whether the screens take the commands now.
    using Gate = std::function<bool()>;

    explicit QcGamepadMenuInput(QObject *parent = nullptr);
    ~QcGamepadMenuInput() override;

    // The pads to follow (nullptr: none); the source must outlive this object or be set to another.
    void setSource(QcGamepadSource *source);
    // Without a gate (the default) the screens always take the commands.
    void setGate(Gate gate) { m_gate = std::move(gate); }

    // Repeats and scrolls up to `nowMs` (the timer passes its clock; tests call it directly).
    void advance(qint64 nowMs);
    // The time of the clock advance() runs on.
    qint64 now() const { return m_clock.elapsed(); }

    // The direction of a state: the d-pad first, else the left stick's main axis.
    static std::optional<Command> direction(const QcGamepadState &raw);

signals:
    void command(QcGamepadMenuInput::Command command);
    // The right stick scrolls by `pixels` (positive: down) since the last advance.
    void scrolled(float pixels);

private:
    void onStateChanged(int id);
    void onPadDisconnected(int id);
    // Emits the buttons pressed since `before`.
    void emitPresses(const QcGamepadState &before, const QcGamepadState &now);
    void updateTimer();
    bool gateOpen() const { return !m_gate || m_gate(); }
    // The gate is closed: drops the held direction and waits for neutral unless every pad is.
    void closeGate();
    // No pad holds a direction or pushes the scrolling stick.
    bool neutral() const;

    QPointer<QcGamepadSource> m_source;
    QHash<int, QcGamepadState> m_states;
    QElapsedTimer m_clock;
    QTimer m_timer;
    // The direction held, by which pad, and when it repeats next.
    std::optional<Command> m_held;
    int m_heldPad = 0;
    qint64 m_nextRepeatMs = 0;
    qint64 m_lastAdvanceMs = 0;
    Gate m_gate;
    // Directions and scrolling wait until every pad is neutral (they were held while the gate was closed).
    bool m_waitForNeutral = false;
};
