#pragma once

#include "QcChunkPos.h"
#include "QcPathTerrain.h"
#include "QcVillage.h"
#include "QcVillager.h"

#include <optional>

class QcPathCellTable;
class QcVillages;
struct QcVillagerSettings;

// Beds and workstations of villagers in the village state (decision E-411): a villager claims a bed
// and a workstation under its name (QcVillage::Spot::claimedBy) and counts in the village's
// population. A claim on a spot that is gone or that someone else holds lapses; a villager that
// dies frees its claims and leaves the population.
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

// A new villager of `village` in its free bed `bed`: named, the bed claimed, the population counted.
// Its name is one no spot of the village is claimed under. Its profession is the unemployed one
// until it claims a workstation.
QcVillager settle(QcVillage &village, QcBlockPos bed, const QcVillagerSettings &settings, quint32 look);
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
void keepClaims(QcVillage &village, QcVillager &villager, const QcVillagerSettings &settings);
// Claims the free bed nearest to `near`; false when there is none.
bool claimBed(QcVillage &village, QcVillager &villager, const QcVec3 &near);
// Claims the free workstation nearest to `near` that its profession works at, or, for an
// unemployed villager, of any profession's kind (it takes that profession: its look changes).
// False when there is none.
bool claimWorkstation(QcVillage &village, QcVillager &villager, const QcVillagerSettings &settings, const QcVec3 &near);
// The villager died or was taken out of the world: its claims are freed, the population counts one
// less.
void release(QcVillages &villages, const QcVillager &villager);

// The next name for a villager of `village` (`villager-<n>`) that no spot of it is claimed under.
QString freshName(QcVillage &village);
// The number in a villager's name (`villager-<n>`), 0 for any other name.
int numberOf(const QString &name);
// A villager of `village` named `name` lives in the world: the next new villager gets a higher
// number (the chunk it was saved with may be newer than villages.json). Whether that changed it.
bool noteName(QcVillage &village, const QString &name);
// Every villager of the village is in the world (`present`, by name): claims under other names
// lapse. The number of their holders.
int dropLostClaims(QcVillage &village, const QSet<QString> &present);

} // namespace QcVillagerHomes
