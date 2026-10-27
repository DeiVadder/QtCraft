#pragma once

#include "clientcore/input/QcVirtualGamepadSource.h"

#include <QList>
#include <QObject>
#include <QStringView>

#include <algorithm>
#include <functional>
#include <optional>

class QcGamepadUi;

// Test hook: a virtual pad played by the startup script (--overlay "@<ms> pad:<step>", decision
// E-583), for screenshots and checks of the pad on the real screens. Steps: "<button>" clicks it
// (pressed, released clickFrames rendered frames later), "+<button>" holds it, "-<button>" lets go,
// "<axis>=<value>" sets an axis (names as in the control interface: south, dpadDown, rightX, ...),
// "connect=<type>" connects another pad of that type (playstation, switchPro, ...; decision E-612)
// and plays it from then on; it is in use after its first press (the first stays connected, so the
// world does not pause).
// Steps run one after another, each once the one before has been reported, so a slow machine
// delays them but never reorders or overlaps them; stepReported() follows each report, so a
// screenshot can wait for the steps instead of the wall clock. reportFrames rendered frames after each step
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
    // Rendered frames between a step and its report (at least reportFrames; a run that checks every
    // step waits as long as its screenshot, QcScreenshotHook::frameCount(), decision E-900).
    void setReportFrames(int frames) { m_reportFrames = std::max(frames, reportFrames); }
    // Queues one step; false (with a warning) for one it cannot read.
    bool run(QStringView step);

    // Whether a startup script line has pad steps.
    static bool wanted(const QStringList &overlays);

signals:
    // A queued step ran and its lines are logged.
    void stepReported();

private:
    // A step of the script, read.
    struct Step
    {
        enum class Kind { Click, Hold, Release, Axis, Connect };
        Kind kind = Kind::Click;
        QcGamepadType type = QcGamepadType::Xbox;
        QcGamepadButton button = QcGamepadButton::South;
        QcGamepadAxis axis = QcGamepadAxis::LeftX;
        float value = 0.0f;
    };

    static std::optional<Step> parse(QStringView step);
    // Runs the next queued step unless one is running.
    void runNext();
    void apply(const Step &step);
    // Calls `then` after `frames` rendered frames of the window (without one, after as many 16 ms).
    void afterFrames(int frames, std::function<void()> then);
    void onFrameSwapped();
    // A pad of `type` connects and takes the following steps.
    void connectAnother(QcGamepadType type);
    // Logs the focus, the radial menu, the keyboard and the family of the button labels, then runs
    // the next step.
    void report();

    QcGamepadUi &m_ui;
    QcVirtualGamepadSource m_source;
    int m_pad = 0;
    QList<Step> m_queue;
    bool m_running = false;
    int m_framesLeft = 0;
    int m_reportFrames = reportFrames;
    std::function<void()> m_afterFrames;
    QMetaObject::Connection m_frameConnection;
};
