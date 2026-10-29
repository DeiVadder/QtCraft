// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcSpawnerDefinition.h"
#include "common/core/QcVec3.h"
#include "common/protocol/QcMessages.h"
#include "server/spawners/QcSpawners.h"

#include <QList>

class QcServerState;
class QcServerWorld;

// How monster spawners work (decision E-520), every game tick: a spawner with a creature kind counts
// its delay down while a player is within its block's `range`; at 0 it runs a cycle: unless
// `maxNearby` creatures of its kind are within `nearbyRange` of its block or the world holds
// QcServerLimits::spawnerCreatures creatures of spawners, it tries `count` random cells of its area
// and spawns a creature on each fitting one (room for the body's height and width, ground below
// unless it flies, block light at most `maxLight` for every kind, sky light not counted). After a
// cycle that spawned (or found its area full) it waits a random delay in [minDelay, maxDelay]; a
// cycle that found no fitting cell tries again in the next tick. Kinds a spawner may not bring forth
// (QcSpawnerContent::refusal) wait. Without a player in the world nothing is done. The creatures are
// ordinary, not persistent, remember their spawner and take its `confined`.
class QcSpawnerRules
{
public:
    explicit QcSpawnerRules(QcServerState &state);

    void tick();
    // Tells the clients that have a chunk about the spawners in it that changed (QcMsg::SpawnerDisplay).
    void announceChanges();
    // What a client is told about the spawner at `pos`: its kind's index, -1 for none.
    static QcMsg::SpawnerDisplay displayOf(const QcServerWorld &world, QcBlockPos pos);
    // Creatures of spawners awake in the world.
    int spawnedCount() const;
    // Creatures of kind `type` within `nearbyRange` of the spawner block at `pos` on every axis.
    int nearbyOf(QcBlockPos pos, const QcSpawnerDefinition &rules, int type) const;
    // Whether a creature of kind `type` may appear with its feet in cell `feet` by these rules.
    bool fits(QcBlockPos feet, const QcSpawnerDefinition &rules, int type) const;

private:
    // Whether the body of `definition` with its feet in `feet` has room: no solid, liquid, hurting or
    // slowing block in the cells it takes up.
    bool hasRoom(QcBlockPos feet, const QcMobDefinition &definition) const;
    // Runs the cycle of a spawner whose delay ran out (`total`: spawner creatures in the world so
    // far); the number of creatures it spawned.
    [[nodiscard]] int cycle(QcSpawners::Entry &spawner, const QcSpawnerDefinition &rules, int type, int total);
    void resetDelay(QcSpawners::Entry &spawner, const QcSpawnerDefinition &rules);
    bool playerNear(const QcVec3 &centre, double range, const QList<QcVec3> &players) const;
    QList<QcVec3> playerPositions() const;

    QcServerState &m_state;
};
