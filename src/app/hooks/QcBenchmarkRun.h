#pragma once

#include "app/hooks/QcFrameProfile.h"
#include "app/hooks/QcPackSwitchHitch.h"
#include "common/core/QcVec3.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <array>

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
// QTCRAFT_BENCHMARK_PACK_SWITCH=<ids> (comma-separated texture pack ids) switches these packs on at
// a third of the run and off again at two thirds, and logs the hitch of each switch (decision E-332).
class QcBenchmarkRun : public QObject
{
    Q_OBJECT

public:
    QcBenchmarkRun(QcGameController &game, const QcSessionParts &parts, QcGameInput &input, const QcSettingsController &settings,
                   QObject *parent = nullptr);

    // `packSwitch`: the texture packs switched on and off during the run (none: no switch).
    void configure(int seconds, const QString &scenario, const QStringList &packSwitch = {});
    // Starts the measurement three seconds after the world became playable, when configured.
    void scheduleStart() const;
    // Time since the previous frame; called first in every frame.
    void recordFrame();
    // CPU time of the GUI thread in this frame (-1 if unknown), the time of its entity meshing and
    // of the character previews' meshing, and the render thread's samples (QcFrameMonitor collects
    // them while the benchmark runs).
    void recordGuiFrame(double guiCpuMs, qint64 entityNs, qint64 previewNs, const QList<QcRenderSample> &samples);
    bool isRunning() const { return m_running; }

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
    // Switch number `index` (0 on, 1 off) of the packs of QTCRAFT_BENCHMARK_PACK_SWITCH.
    void switchPacks(int index);
    void finish();
    // The BENCH line for scripts/bench.sh, perf-check.sh and profile.sh.
    void logBenchLine(const QList<qint64> &sorted, double p99Ms) const;
    // "key=value" pairs of the entities at the end: creatures and item stacks on the server, items
    // in the player's inventory.
    QString entityPairs() const;
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
    QStringList m_packSwitch;
    // The active packs before the first switch, restored by the second.
    QStringList m_packsBefore;
    // Since the start of the measurement, for the pack switches.
    QElapsedTimer m_clock;
    std::array<QcPackSwitchHitch, 2> m_switches;
    // The switch in progress (-1 before the first).
    int m_switch = -1;
};
