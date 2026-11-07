// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "server/core/QcServerConfig.h"

#include <memory>
#include <optional>
#include <vector>

class QcServerDimension;
class QcServerWorld;
struct QcServerClient;

/*! \brief Where players appear in the world, when they join for the first time and when they
    respawn without a soul anchor: in the world's start dimension (bind(), decision E-1110), on
    the surface of the generator's spawn column (or the configured one,
    QcServerConfig::spawnColumn) as the world stores it; when that is no natural ground (a tree, a
    building, water), on the nearest natural ground around it. Under a roof (a dimension with a
    ceiling) the surface is the roof, so there it is the generator's spawn as the world stores it
    when QcSafeStand fits there, else the nearest fitting cell around it (decision E-1111).

    A column without any block gets one under the generator's spawn height, so a respawn never
    drops into the void (decision E-288). */
class QcSpawnPoint
{
public:
    /*! The clients in the dimension of the spawn. */
    using Clients = std::vector<QcServerClient *>;

    QcSpawnPoint(QcServerWorld &world, const QcServerConfig &config, const Clients &clients);

    /*! Puts the spawn into \a dimension: its world, its clients and its roof; what was found
        before is forgotten. */
    void bind(QcServerDimension &dimension);

    /*! Returns the feet of a player appearing now; may generate chunks and place the ground
        block. */
    QcVec3 position();
    /*! Returns the spawn column (x and z; y is the generator's spawn height): the configured one
        or the generator's. Cheap after the first call. */
    QcBlockPos column();

private:
    // Searched on squares of growing radius around the spawn column.
    static constexpr int groundRadius = 16;
    // Under a roof the cells this far above and below the generator's spawn are tried.
    static constexpr int roofReach = 8;
    // Under a roof the columns this far around the generator's spawn are tried (9 x 9).
    static constexpr int roofRadius = 4;

    // The world and the clients of the bound dimension (bind() sets them).
    QcServerWorld &world() const { return *m_world; }
    const Clients &clients() const { return *m_clients; }
    int ceiling() const { return m_ceiling; }
    // The generator's spawn point, searched on the first call.
    QcBlockPos generatorSpawn();
    std::optional<QcBlockPos> lastGround() const { return m_lastGround; }
    void setLastGround(std::optional<QcBlockPos> ground) { m_lastGround = ground; }

    // Nearest stored column around `column` (within groundRadius) whose top block is natural
    // ground; returns that top block.
    std::optional<QcBlockPos> naturalGroundNear(QcBlockPos column);
    // The same on the square ring of `radius` around `column`.
    std::optional<QcBlockPos> naturalGroundOnRing(QcBlockPos column, int radius);
    // The top block of a stored column if it is natural ground, else nullopt.
    std::optional<QcBlockPos> naturalGroundAt(int x, int z);
    // Under a roof: the generator's spawn or the nearest fitting cell around it.
    std::optional<QcBlockPos> standUnderRoof(QcBlockPos spawn);
    // The same on the square ring of `radius` around `spawn`.
    std::optional<QcBlockPos> standOnRing(QcBlockPos spawn, int radius);
    // The first fitting cell of the column at `x`, `z` near the height `y`.
    std::optional<QcBlockPos> standInColumn(int x, int y, int z);
    // Puts the platform block (or stone) at `pos` and tells the clients that have the chunk; false
    // when there is no such block.
    bool placeGround(QcBlockPos pos);

    QcServerWorld *m_world;
    const QcServerConfig &m_config;
    const Clients *m_clients;
    // The height of the dimension's roof; 0 for an open sky.
    int m_ceiling = 0;
    // The generator's spawn point, searched once (it may generate chunks to find open ground).
    std::optional<QcBlockPos> m_generatorSpawn;
    // The natural ground found last time.
    std::optional<QcBlockPos> m_lastGround;
};
