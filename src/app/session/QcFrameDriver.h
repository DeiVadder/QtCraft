#pragma once

#include "common/core/QcVec3.h"

#include <QElapsedTimer>

#include <functional>
#include <memory>

class QcBenchmarkRun;
class QcGamepadControls;
class QcGameInput;
class QcHudState;
class QcPreviewSource;
class QcRenderBridge;
class QcSettingsController;
class QcStartupHooks;
class QcStatsLog;
struct QcSessionParts;

// The frame clock of the running world (decision E-530): every frame of the window it takes the
// input of the frame (keyboard and mouse, then the gamepad), interpolates the player between the
// last two ticks, hands camera, entities, hand, sky, weather and selection to the render bridge,
// updates the HUD, focuses and pumps the mesh scheduler and records the frame for the debug
// display, the benchmark and the statistics log. The one place that reads the session's state
// once per frame. Lives in the GUI thread beside
// QcGameController, which owns it; it refers only to parts declared before it.
class QcFrameDriver
{
public:
    // What a frame is handed to; all of them outlive this object.
    struct Targets
    {
        QcSettingsController &settings;
        QcGameInput &input;
        QcGamepadControls &gamepad;
        QcPreviewSource &preview;
        QcRenderBridge &render;
        QcHudState &hud;
        QcBenchmarkRun &benchmark;
        QcStartupHooks &startup;
    };

    // `playing` says whether the game shows the running world (QcGameController::Phase::Playing).
    QcFrameDriver(QcSessionParts &parts, const Targets &targets, std::function<bool()> playing);
    ~QcFrameDriver();

    // A session starts: the clock since the last tick starts (it may not have run before).
    void startTickClock();
    // The session ran a tick: the interpolation between ticks starts again.
    void restartTickClock();
    // --stats / QTCRAFT_STATS: the debug data as a JSON line in the log every `intervalMs` while a
    // world runs (decision E-225); 0 leaves it off.
    void startStatsLog(int intervalMs);
    // One frame of the window (QQuickWindow::afterAnimating).
    void onFrame();

private:
    // Seconds since the last frame (at most 0.1).
    float advanceFrameClocks();

    QcSessionParts &m_parts;
    Targets m_targets;
    std::function<bool()> m_playing;
    QElapsedTimer m_sinceTick;
    QElapsedTimer m_frameClock;
    // The player's feet as drawn in the latest frame (interpolated between ticks).
    QcVec3 m_drawnFeet;
    std::unique_ptr<QcStatsLog> m_statsLog;
};
