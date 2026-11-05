// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <algorithm>
#include <array>
#include <optional>

class QcClientWorld;
class QcMeshScheduler;

// How far a world start has come (decision E-943), for the loading screen (GameController.loading
// in QML, type LoadingProgress): the stage as an enum, the chunks of the spawn area that arrived and that are meshed,
// and the share of the whole start. Driven by real state only: QcGameController reports the steps
// it runs (content, world, session) and every frame the spawn area as the client world and the mesh
// scheduler hold it (measure()). The stages only go forward; `ready()` comes once the spawn area is
// meshed and the session plays, which is when the world becomes playable. Lives in the GUI thread.
class QcLoadingProgress : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(LoadingProgress)
    QML_UNCREATABLE("Provided by GameController.loading")
    Q_PROPERTY(Stage stage READ stage NOTIFY changed FINAL)
    Q_PROPERTY(Kind kind READ kind NOTIFY changed FINAL)
    // The display name of the dimension a Dimension start goes to; empty for the other kinds.
    Q_PROPERTY(QString destination READ destination NOTIFY changed FINAL)
    // Only joining a network server can be cancelled (cancelling leaves the game); entering another
    // dimension cannot, its longest wait ends it anyway.
    Q_PROPERTY(bool cancellable READ cancellable NOTIFY changed FINAL)
    Q_PROPERTY(int chunksArrived READ chunksArrived NOTIFY changed FINAL)
    Q_PROPERTY(int chunksMeshed READ chunksMeshed NOTIFY changed FINAL)
    Q_PROPERTY(int chunksNeeded READ chunksNeeded NOTIFY changed FINAL)
    // 0..1 for the whole start (fixed shares per stage, the chunk stages by their counts).
    Q_PROPERTY(double fraction READ fraction NOTIFY changed FINAL)

public:
    // The steps of a start in their order: nothing running, loading the base game and the mods,
    // opening or creating the world (the in-process server), waiting for the server's welcome and the
    // player's place (also a network game's start), the chunks of the spawn area arriving, their
    // meshes being built, playable.
    enum class Stage { Idle, Content, World, Joining, Chunks, Meshes, Ready };
    Q_ENUM(Stage)
    // What is started: a world made just now (its terrain is generated while it loads), a saved
    // world, a network server's world, or another dimension of the world played (decision
    // E-1002; destination() names it).
    enum class Kind { NewWorld, SavedWorld, Network, Dimension };
    Q_ENUM(Kind)

    // The spawn area: chunks in it (inside the bound of the world), those that arrived, and those
    // meshed (arrived, its side neighbours arrived too and none of them waits for a mesh).
    struct Area
    {
        int needed = 0;
        int arrived = 0;
        int meshed = 0;
    };

    // A state the test hook shows without a start (--overlay loading:...).
    struct Shown
    {
        Stage stage = Stage::Content;
        Kind kind = Kind::NewWorld;
        Area area;
    };

    // Chunks from the player's chunk to each side that make the spawn area (7 x 7 chunks; fewer
    // when the server streams less, spawnRadiusFor()).
    static constexpr int spawnRadius = 3;
    // How long the loading screen waits at most for the spawn area once the session plays; then
    // the world shows anyway (with a warning), so a stuck mesh never keeps the player out.
    static constexpr int defaultMaxWaitMs = 20000;

    explicit QcLoadingProgress(QObject *parent = nullptr);

    Stage stage() const { return m_stage; }
    Kind kind() const { return m_kind; }
    const QString &destination() const { return m_destination; }
    bool cancellable() const { return m_kind == Kind::Network; }
    const Area &area() const { return m_area; }
    int chunksArrived() const { return area().arrived; }
    int chunksMeshed() const { return area().meshed; }
    int chunksNeeded() const { return area().needed; }
    double fraction() const;
    // A start is running (not idle and not ready, and not a state the test hook only shows).
    bool active() const { return m_stage != Stage::Idle && m_stage != Stage::Ready && !shownOnly(); }

    // A start of `kind` begins: the content stage, or for a network game and another dimension the
    // joining stage; `destination` names the dimension of a Dimension start.
    void begin(Kind kind, const QString &destination = {});
    // The step before `next` is done: World after Content, Joining after World. Any other step is
    // refused with a warning.
    void advance(Stage next);
    // The spawn area as it is now and whether the session plays (the server placed the player).
    // Moves to the chunk, mesh and ready stages; the stage never goes back.
    void observe(Area area, bool sessionPlaying);
    // Back to idle (the start ended without a world: error, cancel, disconnect).
    void reset();
    // Test hook (--overlay loading:...): shows `shown` without a start; observe() leaves it until
    // the next begin() or reset().
    void show(const Shown &shown);
    // Tests: the longest wait for the spawn area once the session plays.
    void setMaxWaitMs(int ms) { m_maxWaitMs = ms; }

    // The spawn radius for a server that streams `streamed` chunks around the player (Welcome): the
    // outermost streamed ring has no neighbours beyond and is never meshed, so at most one less.
    [[nodiscard]] static int spawnRadiusFor(int streamed) { return std::clamp(streamed - 1, 0, spawnRadius); }
    // The spawn area around `centre` in `world`, meshed as `meshes` says.
    [[nodiscard]] static Area measure(const QcClientWorld &world, const QcMeshScheduler &meshes, QcChunkPos centre, int radius = spawnRadius);
    // "loading:<stage>[,<kind>[,<arrived>,<meshed>,<needed>]]" without the "loading:" (test hook).
    // Nullopt for an unknown stage or kind, for Idle and Ready (no loading screen), for a count
    // that is no whole number from 0 on, and for counts with meshed > arrived or arrived > needed.
    [[nodiscard]] static std::optional<Shown> parseShow(QStringView spec);

signals:
    void changed();
    // The world is playable (sent once per start).
    void ready();

private:
    void setStage(Stage stage);
    bool shownOnly() const { return m_shownOnly; }
    void setShownOnly(bool shownOnly) { m_shownOnly = shownOnly; }
    void setArea(Area area) { m_area = area; }
    // Since begin(); invalid before the first start.
    const QElapsedTimer &sinceBegin() const { return m_sinceBegin; }
    // The end of the stage at `index` in ms since begin(), -1 while not reached.
    qint64 stageEndMs(std::size_t index) const { return m_stageEndMs.at(index); }
    void setStageEndMs(std::size_t index, qint64 ms) { m_stageEndMs.at(index) = ms; }
    // Since observe() first saw the session play; invalid before.
    const QElapsedTimer &sincePlaying() const { return m_sincePlaying; }
    // Starts that clock the first time.
    void notePlaying()
    {
        if (!sincePlaying().isValid()) {
            m_sincePlaying.start();
        }
    }
    // "World start: playable after ..." with the time of each stage.
    void logTimes() const;

    Stage m_stage = Stage::Idle;
    Kind m_kind = Kind::NewWorld;
    QString m_destination;
    Area m_area;
    int m_maxWaitMs = defaultMaxWaitMs;
    // The state comes from show() (test hook), not from a start.
    bool m_shownOnly = false;
    // Since begin(), the end of each stage in ms since then (-1 while not reached), and since the
    // session plays.
    QElapsedTimer m_sinceBegin;
    std::array<qint64, 7> m_stageEndMs{-1, -1, -1, -1, -1, -1, -1};
    QElapsedTimer m_sincePlaying;
};
