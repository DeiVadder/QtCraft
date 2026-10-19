#pragma once

#include "clientcore/input/QcVirtualGamepadSource.h"

#include <QList>
#include <QObject>
#include <QStringView>

#include <functional>
#include <optional>

class QcGamepadUi;

// Test hook: a virtual pad played by the startup script (--overlay "@<ms> pad:<step>", decision
// E-583), for screenshots and checks of the pad on the real screens. Steps: "<button>" clicks it
// (pressed, released clickFrames rendered frames later), "+<button>" holds it, "-<button>" lets go,
// "<axis>=<value>" sets an axis (names as in the control interface: south, dpadDown, rightX, ...).
// Steps run one after another, each once the one before has been reported, so a slow machine
// delays them but never reorders or overlaps them. reportFrames rendered frames after each step
// the log gets a line "padFocus: <focused item>", while it is open "padRadial: <slot>", and while
// the on-screen keyboard is open "padKeyboard: <text>" (as the field shows it: a password's dots).
class QcScriptedPad : public QObject
{
    Q_OBJECT

public:
    static constexpr int clickFrames = 4;
    // The world's pad acts once per frame, a screen that opens needs a few to settle.
    static constexpr int reportFrames = 8;

    explicit QcScriptedPad(QcGamepadUi &ui, QObject *parent = nullptr);

    QcGamepadSource *source() { return &m_source; }
    // Queues one step; false (with a warning) for one it cannot read.
    bool run(QStringView step);

    // Whether a startup script line has pad steps.
    static bool wanted(const QStringList &overlays);

private:
    // A step of the script, read.
    struct Step
    {
        enum class Kind { Click, Hold, Release, Axis };
        Kind kind = Kind::Click;
        QcGamepadButton button = QcGamepadButton::South;
        QcGamepadAxis axis = QcGamepadAxis::LeftX;
        float value = 0.0f;
    };

    static std::optional<Step> parse(QStringView step);
    // Runs the next queued step unless one is running.
    void runNext();
    void apply(Step step);
    // Calls `then` after `frames` rendered frames of the window (without one, after as many 16 ms).
    void afterFrames(int frames, std::function<void()> then);
    void onFrameSwapped();
    // Logs the focus, the radial menu and the keyboard, then runs the next step.
    void report();

    QcGamepadUi &m_ui;
    QcVirtualGamepadSource m_source;
    int m_pad = 0;
    QList<Step> m_queue;
    bool m_running = false;
    int m_framesLeft = 0;
    std::function<void()> m_afterFrames;
    QMetaObject::Connection m_frameConnection;
};
