#pragma once

#include "QcBlockPos.h"
#include "QcServerConfig.h"
#include "QcVec3.h"

#include <memory>
#include <optional>
#include <vector>

class QcServerWorld;
struct QcServerClient;

// Where players appear in the world, when they join for the first time and when they respawn: on
// the surface of the generator's spawn column (or the configured one, QcServerConfig::spawnColumn)
// as the world stores it; when that is no natural ground (a tree, a building, water), on the
// nearest natural ground around it. A column without any block gets one under the generator's spawn
// height, so a respawn never drops into the void (decision E-288).
class QcSpawnPoint
{
public:
    using Clients = std::vector<std::unique_ptr<QcServerClient>>;

    QcSpawnPoint(QcServerWorld &world, const QcServerConfig &config, const Clients &clients);

    // The feet of a player appearing now; may generate chunks and place the ground block.
    QcVec3 position();
    // The spawn column (x and z; y is the generator's spawn height): the configured one or the
    // generator's. Cheap after the first call.
    QcBlockPos column();

private:
    // Searched on squares of growing radius around the spawn column.
    static constexpr int groundRadius = 16;

    // Nearest stored column around `column` (within groundRadius) whose top block is natural
    // ground; returns that top block.
    std::optional<QcBlockPos> naturalGroundNear(QcBlockPos column);
    // The same on the square ring of `radius` around `column`.
    std::optional<QcBlockPos> naturalGroundOnRing(QcBlockPos column, int radius);
    // The top block of a stored column if it is natural ground, else nullopt.
    std::optional<QcBlockPos> naturalGroundAt(int x, int z);
    // Puts the platform block (or stone) at `pos` and tells the clients that have the chunk; false
    // when there is no such block.
    bool placeGround(QcBlockPos pos);

    QcServerWorld &m_world;
    const QcServerConfig &m_config;
    const Clients &m_clients;
    // The generator's spawn point, searched once (it may generate chunks to find open ground).
    std::optional<QcBlockPos> m_generatorSpawn;
    // The natural ground found last time.
    std::optional<QcBlockPos> m_lastGround;
};
