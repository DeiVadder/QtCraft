// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/core/QcSpawnPoint.h"

#include "server/core/QcServerClient.h"
#include "server/core/QcServerWorld.h"

#include <algorithm>
#include <utility>

QcSpawnPoint::QcSpawnPoint(QcServerWorld &world, const QcServerConfig &config, const Clients &clients)
    : m_world(world)
    , m_config(config)
    , m_clients(clients)
{
}

QcBlockPos QcSpawnPoint::column()
{
    if (!m_generatorSpawn) {
        m_generatorSpawn = m_world.generator().spawnPoint();
    }
    if (m_config.spawnColumn) {
        return {(*m_config.spawnColumn)[0], 0, (*m_config.spawnColumn)[1]};
    }
    return *m_generatorSpawn;
}

QcVec3 QcSpawnPoint::position()
{
    QcBlockPos spawn = column();

    if (!m_config.spawnColumn) {
        // The column found last time is looked at first: the spawn chunks are usually not loaded
        // (stage 31.3), and a full search would read every chunk it passes.
        std::optional<QcBlockPos> ground = m_lastGround ? naturalGroundAt(m_lastGround->x, m_lastGround->z) : std::nullopt;
        if (!ground) {
            ground = naturalGroundNear(spawn);
        }
        if (ground) {
            m_lastGround = ground;
            return {ground->x + 0.5, static_cast<double>(ground->y + 1), ground->z + 0.5};
        }
    }

    // Stand on whatever the stored column contains (the test hook's column, or no ground nearby).
    const std::shared_ptr<const QcChunk> spawnChunk = m_world.loadChunk(spawn.chunk());
    spawn.y = spawnChunk->highestNonAir(spawn.localX(), spawn.localZ()) + 1;

    // An empty column (a void world without platform, a cleared spawn) would drop the player into
    // the void again and again.
    if (spawn.y == 0) {
        const int spawnHeight = m_generatorSpawn.value_or(QcBlockPos{}).y;
        const QcBlockPos ground{spawn.x, std::clamp(spawnHeight - 1, 0, QcWorld::worldHeight - 3), spawn.z};
        if (placeGround(ground)) {
            spawn.y = ground.y + 1;
        }
    }
    return {spawn.x + 0.5, static_cast<double>(spawn.y), spawn.z + 0.5};
}

bool QcSpawnPoint::placeGround(QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_world.generator().platformBlock().or_else([this] {
        return m_world.content().blockId(QStringLiteral("qtcraft:stone"));
    });
    if (!block || !m_world.setBlock(pos, *block)) {
        return false;
    }

    for (const auto &client : m_clients) {
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
        for (const auto [dx, dz] : sides) {
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
    const std::shared_ptr<const QcChunk> chunk = m_world.loadChunk(top.chunk());
    top.y = chunk->highestNonAir(top.localX(), top.localZ());
    if (top.y < 0 || !m_world.generator().isNaturalGround(chunk->block(top.localX(), top.y, top.localZ()))) {
        return std::nullopt;
    }
    return top;
}
