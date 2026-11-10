// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/core/QcSpawnPoint.h"

#include "common/blockrules/QcSafeStand.h"
#include "server/core/QcServerClient.h"
#include "server/core/QcServerWorld.h"
#include "server/dimensions/QcServerDimension.h"

#include <algorithm>
#include <utility>

QcSpawnPoint::QcSpawnPoint(QcServerWorld &world, const QcServerConfig &config, const Clients &clients)
    : m_world(&world)
    , m_config(config)
    , m_clients(&clients)
{
}

void QcSpawnPoint::bind(QcServerDimension &dimension)
{
    m_world = &dimension.world();
    m_clients = &dimension.clients();
    m_ceiling = dimension.definition().ceiling;
    m_generatorSpawn.reset();
    m_lastGround.reset();
}

QcBlockPos QcSpawnPoint::generatorSpawn()
{
    if (!m_generatorSpawn) {
        m_generatorSpawn = world().generator().spawnPoint();
    }
    return *m_generatorSpawn;
}

QcBlockPos QcSpawnPoint::column()
{
    const QcBlockPos spawn = generatorSpawn();
    if (m_config.spawnColumn) {
        // The generator's height: under a roof the search starts there, not in the bedrock.
        return {(*m_config.spawnColumn)[0], spawn.y, (*m_config.spawnColumn)[1]};
    }
    return spawn;
}

QcVec3 QcSpawnPoint::position()
{
    QcBlockPos spawn = column();

    if (ceiling() > 0) {
        if (const std::optional<QcBlockPos> feet = standUnderRoof(spawn)) {
            setLastGround(feet);
            return {feet->x + 0.5, static_cast<double>(feet->y), feet->z + 0.5};
        }
        return {spawn.x + 0.5, static_cast<double>(spawn.y), spawn.z + 0.5};
    }

    if (!m_config.spawnColumn) {
        // The column found last time is looked at first: the spawn chunks are usually not loaded
        // (stage 31.3), and a full search would read every chunk it passes.
        std::optional<QcBlockPos> ground = lastGround() ? naturalGroundAt(lastGround()->x, lastGround()->z) : std::nullopt;
        if (!ground) {
            ground = naturalGroundNear(spawn);
        }
        if (ground) {
            setLastGround(ground);
            return {ground->x + 0.5, static_cast<double>(ground->y + 1), ground->z + 0.5};
        }
    }

    // Stand on whatever the stored column contains (the test hook's column, or no ground nearby).
    const std::shared_ptr<const QcChunk> spawnChunk = world().loadChunk(spawn.chunk());
    spawn.y = spawnChunk->highestNonAir(spawn.localX(), spawn.localZ()) + 1;

    // An empty column (a void world without platform, a cleared spawn) would drop the player into
    // the void again and again.
    if (spawn.y == 0) {
        const int spawnHeight = generatorSpawn().y;
        const QcBlockPos ground{spawn.x, std::clamp(spawnHeight - 1, 0, QcWorld::worldHeight - 3), spawn.z};
        if (placeGround(ground)) {
            spawn.y = ground.y + 1;
        }
    }
    return {spawn.x + 0.5, static_cast<double>(spawn.y), spawn.z + 0.5};
}

bool QcSpawnPoint::placeGround(QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world().generator().platformBlock().or_else([this] {
        return world().content().blockId(QStringLiteral("qtcraft:stone"));
    });
    if (!block || !world().setBlock(pos, *block)) {
        return false;
    }

    for (const auto &client : clients()) {
        if (client->state == QcServerClient::State::Playing && client->view.sentChunks.contains(pos.chunk())) {
            client->connection->send(QcMsg::BlockUpdate{pos, *block});
        }
    }
    return true;
}

std::optional<QcBlockPos> QcSpawnPoint::naturalGroundNear(QcBlockPos column)
{
    for (int radius = 0; radius <= groundRadius; ++radius) {
        if (const std::optional<QcBlockPos> ground = naturalGroundOnRing(column, radius)) {
            return ground;
        }
    }
    return std::nullopt;
}

std::optional<QcBlockPos> QcSpawnPoint::naturalGroundOnRing(QcBlockPos column, int radius)
{
    // Each side of the square ring is walked once (corners twice, which is harmless).
    for (int i = -radius; i <= radius; ++i) {
        const std::pair<int, int> sides[] = {{i, -radius}, {i, radius}, {-radius, i}, {radius, i}};
        for (const auto &[dx, dz] : sides) {
            if (const std::optional<QcBlockPos> ground = naturalGroundAt(column.x + dx, column.z + dz)) {
                return ground;
            }
        }
    }
    return std::nullopt;
}

std::optional<QcBlockPos> QcSpawnPoint::naturalGroundAt(int x, int z)
{
    QcBlockPos top{x, 0, z};
    const std::shared_ptr<const QcChunk> chunk = world().loadChunk(top.chunk());
    top.y = chunk->highestNonAir(top.localX(), top.localZ());
    if (top.y < 0 || !world().generator().isNaturalGround(chunk->block(top.localX(), top.y, top.localZ()))) {
        return std::nullopt;
    }
    return top;
}

std::optional<QcBlockPos> QcSpawnPoint::standUnderRoof(QcBlockPos spawn)
{
    // The cell found last time first, as on the surface.
    if (const std::optional<QcBlockPos> last = lastGround()) {
        world().loadChunk(last->chunk());
        if (QcSafeStand::fits(world(), *last)) {
            return last;
        }
    }
    for (int radius = 0; radius <= roofRadius; ++radius) {
        if (const std::optional<QcBlockPos> feet = standOnRing(spawn, radius)) {
            return feet;
        }
    }
    return std::nullopt;
}

std::optional<QcBlockPos> QcSpawnPoint::standOnRing(QcBlockPos spawn, int radius)
{
    for (int i = -radius; i <= radius; ++i) {
        const std::pair<int, int> sides[] = {{i, -radius}, {i, radius}, {-radius, i}, {radius, i}};
        for (const auto &[dx, dz] : sides) {
            if (const std::optional<QcBlockPos> feet = standInColumn(spawn.x + dx, spawn.y, spawn.z + dz)) {
                return feet;
            }
        }
    }
    return std::nullopt;
}

std::optional<QcBlockPos> QcSpawnPoint::standInColumn(int x, int y, int z)
{
    // The cells around a column may lie in the chunks beside it.
    const QcChunkPos chunk = QcBlockPos{x, 0, z}.chunk();
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            world().loadChunk({chunk.x + dx, chunk.z + dz});
        }
    }
    // Nearest the generator's height first: 0, +1, -1, +2, -2 ...
    for (int step = 0; step <= 2 * roofReach; ++step) {
        const int dy = (step + 1) / 2 * (step % 2 == 1 ? 1 : -1);
        const QcBlockPos feet{x, y + dy, z};
        // fits() checks the cells themselves first, the cube of hazards only for those.
        if (feet.y > 0 && feet.y + 1 < ceiling() && QcSafeStand::fits(world(), feet)) {
            return feet;
        }
    }
    return std::nullopt;
}
