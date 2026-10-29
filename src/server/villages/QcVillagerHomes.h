// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"
#include "server/pathing/QcPathTerrain.h"
#include "server/villages/QcVillage.h"
#include "server/villages/QcVillager.h"

#include <optional>

class QcPathCellTable;
class QcVillages;
struct QcVillagerSettings;

// Beds and workstations of villagers in the village state (decision E-411): a villager claims a bed
// and a workstation under its creature's persistent id (`holder`, QcCreature::uid, decision E-420;
// QcVillage::Spot::claimedBy) and counts in the village's population. A claim on a spot that is gone
// or that someone else holds lapses; a villager that dies frees its claims and leaves the population.
namespace QcVillagerHomes {

// Villagers living in one village and in the world at most (QcServerLimits villagersPerVillage,
// villagers): no villager spawns beyond them.
struct Limits
{
    qsizetype perVillage = 32;
    qsizetype world = 1024;
};

// A villager holding claims strays at most this far beyond the village's reach (strolls, legs and
// flight go home); with every chunk of that area loaded, every one of them is in the world.
inline constexpr int strayReach = 16;

// A new villager of `village` with the persistent id `holder` in its free bed `bed`: the bed
// claimed, the population counted. Its profession is the unemployed one until it claims a
// workstation.
QcVillager settle(QcVillage &village, QcBlockPos bed, quint64 holder, const QcVillagerSettings &settings, quint32 look);
// The free beds of the village whose heads lie in `chunk`, in the village's order.
QList<QcBlockPos> freeBedsIn(const QcVillage &village, QcChunkPos chunk);

// Where a body stands beside the bed whose head is at `head`: the cell next to it (around the foot
// first) that it can stand in, nearest to the foot; none when the bed is walled in.
std::optional<QcVec3> besideBed(const QcBlockAccess &world, const QcPathCellTable &cells, QcBlockPos head, QcPathRules rules);
// Where and how a villager lies in the bed whose head is at `head`: on top of it, the head on the
// pillow, facing the foot (the client lays it on its back, QcAnimator). None when no bed is there.
struct Lying
{
    QcVec3 position;
    float yaw = 0.0f;
};
std::optional<Lying> lyingIn(const QcBlockAccess &world, QcBlockPos head);

// Drops the villager's claims on spots of `village` that are gone or held by another; one that lost
// its workstation before it gained any experience becomes unemployed again (its look changes).
void keepClaims(QcVillage &village, QcVillager &villager, quint64 holder, const QcVillagerSettings &settings);
// Claims the free bed nearest to `near`; false when there is none.
bool claimBed(QcVillage &village, QcVillager &villager, quint64 holder, const QcVec3 &near);
// Claims the free workstation nearest to `near` that its profession works at, or, for an
// unemployed villager, of any profession's kind (it takes that profession: its look changes).
// False when there is none.
bool claimWorkstation(QcVillage &village, QcVillager &villager, quint64 holder, const QcVillagerSettings &settings, const QcVec3 &near);
// The villager died or was taken out of the world: its claims are freed, the population counts one
// less unless the village was reconciled without it (QcVillage::Residents), and a settlement left
// without spots and villagers is forgotten.
void release(QcVillages &villages, const QcVillager &villager, quint64 holder);

// The villager leaves its village for good while it lives (relocated, decision E-490): its claims
// are freed, the village counts one less (a settlement left empty is forgotten) and it is homeless
// (village 0); one without experience loses its profession with its workstation.
void moveOut(QcVillages &villages, QcVillager &villager, quint64 holder, const QcVillagerSettings &settings);
// The homeless villager moves into `village`: it counts there, a settlement without a style takes
// its style, and it claims the free bed nearest to `near`.
void moveIn(QcVillage &village, QcVillager &villager, quint64 holder, const QcVec3 &near);

// Every villager of the village is in the world (`present`, by persistent id): claims under other
// ids lapse. The number of their holders.
int dropLostClaims(QcVillage &village, const QSet<quint64> &present);

} // namespace QcVillagerHomes
