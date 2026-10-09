#pragma once

#include "QcPlayerInput.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

class QcBenchmarkRun;
class QcGameController;
class QcScreenshotHook;
struct QcSessionParts;

// Test hooks from the command line for reproducible scenes: view direction, spawn column and
// frozen pose of the next world, chat lines and screens once it plays, and the screenshot, held
// until the world is loaded and the script has run. Owns the benchmark run.
class QcStartupHooks : public QObject
{
    Q_OBJECT

public:
    QcStartupHooks(QcGameController &game, const QcSessionParts &parts, QcBenchmarkRun *benchmark, QObject *parent = nullptr);

    void setInitialLook(float yaw, float pitch);
    // The player's view direction when a world starts.
    QcPlayerInput initialIntent() const;
    void setSpawnColumn(int x, int z) { m_spawnColumn = std::array<int, 2>{x, z}; }
    std::optional<std::array<int, 2>> spawnColumn() const { return m_spawnColumn; }
    // Freezes the local player's pose ("walk", "swing", "swim", "jump", "sneak", "death").
    void setForcedPose(const QString &pose) { m_forcedPose = pose; }
    QString forcedPose() const { return m_forcedPose; }
    // Chat lines sent and overlays opened once the world is playable. Like chat lines, an overlay
    // "@<ms> <name>" opens that many milliseconds later ("@<ms> " closes the overlay).
    void setStartupScript(QStringList chatLines, const QStringList &overlays);
    // --handbook: opened once the world plays (at once without --play).
    void setStartupHandbook(const QString &target) { m_startupHandbook = target; }
    // The screenshot to take once the world is loaded.
    void setScreenshotHook(QcScreenshotHook *hook);
    QcBenchmarkRun *benchmark() const { return m_benchmark; }

    // Runs the script: the world has just become playable.
    void worldPlaying();
    // --overlay furnace|chest: fills the machine that just opened from the hotbar.
    void machineOpened();
    // Releases the screenshot once every chunk in view arrived and is meshed.
    void releaseScreenshotWhenLoaded(int viewDistance, bool playing);

private:
    void sendStartupChat();
    void openStartupOverlay();
    // Uses the block in view once the startup commands have placed it.
    void openMachineInView();
    void scheduleOverlaySteps();
    // Holds the screenshot until the matching releaseScreenshot().
    void holdScreenshot() const;
    void releaseScreenshot() const;

    QcGameController &m_game;
    const QcSessionParts &m_parts;
    QcBenchmarkRun *m_benchmark = nullptr;
    float m_initialYaw = 0.0f;
    float m_initialPitch = 0.0f;
    std::optional<std::array<int, 2>> m_spawnColumn;
    QString m_forcedPose;
    QStringList m_startupChat;
    QString m_startupOverlay;
    QStringList m_startupOverlaySteps;
    QString m_startupHandbook;
    // --overlay furnace: the screenshot waits until the machine has opened and filled.
    bool m_fillMachineOnOpen = false;
    QPointer<QcScreenshotHook> m_screenshotHook;
    bool m_screenshotReleased = false;
};
