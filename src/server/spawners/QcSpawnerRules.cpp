// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/spawners/QcSpawnerRules.h"

#include "common/content/registry/QcSpawnerContent.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerState.h"

#include <algorithm>
#include <cmath>

QcSpawnerRules::QcSpawnerRules(QcServerState &state)
    : m_state(state)
{
}

void QcSpawnerRules::tick()
{
    QcServerWorld &world = m_state.world();
    QcSpawners &spawners = world.spawners();
    if (spawners.size() == 0) {
        return;
    }

    // Without a player nothing counts down: no work at all.
    const QList<QcVec3> players = playerPositions();
    if (players.isEmpty()) {
        return;
    }

    int total = -1;
    const QList<QcBlockPos> positions = spawners.positions();
    for (const QcBlockPos &pos : positions) {
        QcSpawners::Entry *spawner = spawners.at(pos);
        const std::optional<QcBlockId> block = world.blockAt(pos);
        if (!spawner || spawner->mob.isEmpty() || !block) {
            continue;
        }
        const QcSpawnerDefinition &rules = m_state.content().block(*block).spawner;
        if (!playerNear({pos.x + 0.5, pos.y + 0.5, pos.z + 0.5}, rules.range, players)) {
            continue;
        }
        const std::optional<int> type = m_state.content().mobIndex(spawner->mob);
        if (!type || !QcSpawnerContent::refusal(m_state.content().mobs()[*type]).isEmpty()) {
            continue;
        }
        if (spawner->delay > 0) {
            --spawner->delay;
            continue;
        }

        // Counted once per tick, and only when a cycle runs.
        if (total < 0) {
            total = spawnedCount();
        }
        total += cycle(*spawner, rules, *type, total);
    }
}

int QcSpawnerRules::cycle(QcSpawners::Entry &spawner, const QcSpawnerDefinition &rules, int type, int total)
{
    const qint64 limit = m_state.config().limits.spawnerCreatures;
    int nearby = nearbyOf(spawner.pos, rules, type);
    if (nearby >= rules.maxNearby || total >= limit) {
        resetDelay(spawner, rules);
        return 0;
    }

    QRandomGenerator &random = m_state.random();
    QcMobManager &mobs = m_state.entities().creatures();
    int spawned = 0;
    for (int attempt = 0; attempt < rules.count && nearby < rules.maxNearby && total + spawned < limit; ++attempt) {
        const int dx = random.bounded(rules.areaX) - rules.areaX / 2;
        const int dy = random.bounded(rules.areaY) - rules.areaY / 2;
        const int dz = random.bounded(rules.areaZ) - rules.areaZ / 2;
        const QcBlockPos feet = spawner.pos.offset(dx, dy, dz);
        if (!fits(feet, rules, type)) {
            continue;
        }

        // Under rock it stays by day like creatures of caves.
        const std::shared_ptr<const QcChunk> chunk = m_state.world().chunk(feet.chunk());
        const bool covered = chunk && chunk->highestNonAir(feet.localX(), feet.localZ()) > feet.y + 1;
        mobs.spawn(type, {feet.x + 0.5, static_cast<double>(feet.y), feet.z + 0.5}, m_state.entities().newId(), covered, false);
        QcCreature &creature = mobs.editableMobs().last();
        creature.spawner = spawner.pos;
        creature.confined = spawner.confined;
        ++spawned;
        ++nearby;
    }
    if (spawned > 0) {
        resetDelay(spawner, rules);
    }
    return spawned;
}

void QcSpawnerRules::resetDelay(QcSpawners::Entry &spawner, const QcSpawnerDefinition &rules)
{
    const double seconds = rules.minDelay + m_state.random().generateDouble() * (rules.maxDelay - rules.minDelay);
    spawner.delay = static_cast<quint32>(std::lround(seconds * QcPhysics::tickRate));
}

bool QcSpawnerRules::fits(QcBlockPos feet, const QcSpawnerDefinition &rules, int type) const
{
    const QcServerWorld &world = m_state.world();
    const QcContentRegistry &content = m_state.content();
    const QcMobDefinition &definition = content.mobs()[type];
    if (!hasRoom(feet, definition)) {
        return false;
    }
    const std::optional<QcBlockId> ground = world.blockAt(feet.offset(0, -1, 0));
    if (!definition.flying && (!ground || !content.block(*ground).solid)) {
        return false;
    }
    return world.blockLight(feet) <= rules.maxLight;
}

bool QcSpawnerRules::hasRoom(QcBlockPos feet, const QcMobDefinition &definition) const
{
    const QcServerWorld &world = m_state.world();
    const QcContentRegistry &content = m_state.content();
    const auto open = [&](QcBlockPos cell) {
        const std::optional<QcBlockId> block = world.blockAt(cell);
        if (!block) {
            return false;
        }
        const QcBlockDefinition &kind = content.block(*block);
        return !kind.solid && !kind.liquid && kind.contactDamage <= 0.0 && kind.slowsInside >= 1.0;
    };

    // Every cell the body, centred on the cell, takes up (a spider's width takes the cells around).
    const int cells = std::max(1, static_cast<int>(std::ceil(definition.height - 1e-6)));
    const int side = std::max(0, static_cast<int>(std::ceil(definition.width / 2.0 - 0.5 - 1e-6)));
    const int across = 2 * side + 1;
    for (int cell = 0; cell < cells * across * across; ++cell) {
        if (!open(feet.offset(cell % across - side, cell / (across * across), cell / across % across - side))) {
            return false;
        }
    }
    return true;
}

int QcSpawnerRules::nearbyOf(QcBlockPos pos, const QcSpawnerDefinition &rules, int type) const
{
    // The spawner's block grown by `nearbyRange` on every side, independent of the spawn area.
    const double reach = rules.nearbyRange + 0.5;
    const QcVec3 centre{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
    return static_cast<int>(std::ranges::count_if(m_state.entities().creatures().mobs(), [&](const QcCreature &mob) {
        const QcVec3 d = mob.motion.position - centre;
        return mob.type == type && !mob.dormant && std::abs(d.x) <= reach && std::abs(d.y) <= reach && std::abs(d.z) <= reach;
    }));
}

void QcSpawnerRules::announceChanges()
{
    const QList<QcBlockPos> changes = m_state.world().spawners().takeChanges();
    for (const QcBlockPos &pos : changes) {
        const QcMsg::SpawnerDisplay display = displayOf(m_state.world(), pos);
        for (const auto &client : m_state.clients()) {
            if (client->state == QcServerClient::State::Playing && client->view.sentChunks.contains(pos.chunk())) {
                client->connection->send(display);
            }
        }
    }
}

QcMsg::SpawnerDisplay QcSpawnerRules::displayOf(const QcServerWorld &world, QcBlockPos pos)
{
    const QcSpawners::Entry *spawner = world.spawners().at(pos);
    const std::optional<int> type = spawner && !spawner->mob.isEmpty() ? world.content().mobIndex(spawner->mob) : std::nullopt;
    return {pos, type ? static_cast<qint32>(*type) : -1};
}

int QcSpawnerRules::spawnedCount() const
{
    return static_cast<int>(std::ranges::count_if(m_state.entities().creatures().mobs(), [](const QcCreature &mob) { return mob.spawner && !mob.dormant; }));
}

bool QcSpawnerRules::playerNear(const QcVec3 &centre, double range, const QList<QcVec3> &players) const
{
    return std::ranges::any_of(players, [&](const QcVec3 &player) { return (player - centre).length() <= range; });
}

QList<QcVec3> QcSpawnerRules::playerPositions() const
{
    QList<QcVec3> positions;
    for (const auto &client : m_state.clients()) {
        // Spectators wake no spawner (decision E-730).
        if (client->hasBody()) {
            positions.append(client->motion.position);
        }
    }
    return positions;
}
