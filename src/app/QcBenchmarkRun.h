#pragma once

#include "QcFrameProfile.h"
#include "QcVec3.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>

class QcGameController;
class QcGameInput;
class QcSettingsController;
class QTimer;
struct QcSessionParts;

// Test hook (QTCRAFT_BENCHMARK=<seconds>): once playing and after a short warm-up, frame and
// server tick times are recorded, then the result is logged and the application quits.
// Benchmark scenarios: "sprint" sprints and jumps ahead, "walk" walks ahead, "stand" stands
// still, "light" stands while torches are placed and removed around the player (block light
// updates and remeshing), "liquids" stands while water and lava sources appear above the floor
// around the player, flood it and drain again (flow steps, liquid meshing).
class QcBenchmarkRun : public QObject
{
    Q_OBJECT

public:
    QcBenchmarkRun(QcGameController &game, const QcSessionParts &parts, QcGameInput &input, const QcSettingsController &settings,
                   QObject *parent = nullptr);

    void configure(int seconds, const QString &scenario);
    // Starts the measurement three seconds after the world became playable, when configured.
    void scheduleStart() const;
    // Time since the previous frame; called first in every frame.
    void recordFrame();
    // CPU time of the GUI thread in this frame, the time of its entity meshing and the render
    // thread's samples.
    void recordGuiFrame(qint64 entityNs);

private:
    void start();
    // Clears the samples and starts collecting them from the server, renderer and mesher.
    void resetMeasurement();
    void startStanding();
    void startWalking();
    // Four times a second a torch appears or vanishes on a ring around the player, so light
    // spreads and goes away again across section and chunk borders all the time.
    void startTorchRing();
    // Every half second a source appears four metres up on a ring around the player (every third
    // one lava) or an earlier one is taken away, so the floor floods and drains all the time.
    void startLiquidRing();
    // Turns away when stuck at a tree or cliff, so the walk keeps loading new chunks.
    void steer(QTimer *timer);
    void finish();
    // The BENCH line for scripts/bench.sh, perf-check.sh and profile.sh.
    void logBenchLine(const QList<qint64> &sorted, double p99Ms) const;
    void logReport(const QList<qint64> &sorted, double p99Ms) const;

    QcGameController &m_game;
    const QcSessionParts &m_parts;
    QcGameInput &m_input;
    const QcSettingsController &m_settings;
    int m_seconds = 0;
    bool m_running = false;
    bool m_walk = true;
    bool m_sprint = true;
    bool m_light = false;
    bool m_liquids = false;
    QElapsedTimer m_frame;
    QcVec3 m_checkpoint;
    QList<qint64> m_frameNs;
    QcFrameProfile m_frameProfile;
    qint64 m_guiCpuNs = -1;
};
