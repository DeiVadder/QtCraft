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

/*! \brief Beds and workstations of villagers in the village state (decision E-411): a villager
    claims a bed and a workstation under its creature's persistent id (\c {holder}, QcCreature::uid,
    decision E-420; QcVillage::Spot::claimedBy) and counts in the village's population.

    A claim on a spot that is gone or that someone else holds lapses; a villager that dies frees
    its claims and leaves the population. */
namespace QcVillagerHomes {

/*! \brief Villagers living in one village and in the world at most (QcServerLimits
    villagersPerVillage, villagers): no villager spawns beyond them. */
struct Limits
{
    /*! Villagers in one village at most. */
    qsizetype perVillage = 32;
    /*! Villagers in the world at most. */
    qsizetype world = 1024;
};

/*! A villager holding claims strays at most this far beyond the village's reach (strolls, legs
    and flight go home); with every chunk of that area loaded, every one of them is in the world. */
inline constexpr int strayReach = 16;

/*! Returns a new villager of \a village with the persistent id \a holder in its free bed \a bed,
    its species \a settings and the seed of its look \a look: the bed claimed, the population
    counted. Its profession is the unemployed one until it claims a workstation. */
QcVillager settle(QcVillage &village, QcBlockPos bed, quint64 holder, const QcVillagerSettings &settings, quint32 look);
/*! Returns the free beds of \a village whose heads lie in \a chunk, in the village's order. */
QList<QcBlockPos> freeBedsIn(const QcVillage &village, QcChunkPos chunk);

/*! Returns where a body under \a rules stands beside the bed of \a world whose head is at
    \a head: the cell next to it (around the foot first) that it can stand in (path cells from
    \a cells), nearest to the foot; none when the bed is walled in. */
std::optional<QcVec3> besideBed(const QcBlockAccess &world, const QcPathCellTable &cells, QcBlockPos head, QcPathRules rules);
/*! \brief Where and how a villager lies in the bed whose head is at \c {head}: on top of it, the
    head on the pillow, facing the foot (the client lays it on its back, QcAnimator).

    None when no bed is there. */
struct Lying
{
    /*! Where it lies. */
    QcVec3 position;
    /*! Where it faces (degrees). */
    float yaw = 0.0f;
};
/*! Returns how a villager lies in the bed of \a world whose head is at \a head. */
std::optional<Lying> lyingIn(const QcBlockAccess &world, QcBlockPos head);

/*! Drops the claims of \a villager (persistent id \a holder) on spots of \a village that are gone
    or held by another; one that lost its workstation before it gained any experience becomes
    unemployed again (its look changes, \a settings). */
void keepClaims(QcVillage &village, QcVillager &villager, quint64 holder, const QcVillagerSettings &settings);
/*! Claims for \a villager (persistent id \a holder) the free bed of \a village nearest to
    \a near; returns false when there is none. */
bool claimBed(QcVillage &village, QcVillager &villager, quint64 holder, const QcVec3 &near);
/*! Claims for \a villager (persistent id \a holder) the free workstation of \a village nearest
    to \a near that its profession works at, or, for an unemployed villager, of any profession's
    kind in \a settings (it takes that profession: its look changes). Returns false when there is
    none. */
bool claimWorkstation(QcVillage &village, QcVillager &villager, quint64 holder, const QcVillagerSettings &settings, const QcVec3 &near);
/*! \a villager (persistent id \a holder) died or was taken out of the world: its claims among \a
    villages are freed, the population counts one less unless the village was reconciled without it
    (QcVillage::Residents), and a settlement left without spots and villagers is forgotten. */
void release(QcVillages &villages, const QcVillager &villager, quint64 holder);

/*! \a villager (persistent id \a holder) leaves its village among \a villages for good while it
    lives (relocated, decision E-490): its claims are freed, the village counts one less (a
    settlement left empty is forgotten) and it is homeless (village 0); one without experience
    loses its profession with its workstation (\a settings). */
void moveOut(QcVillages &villages, QcVillager &villager, quint64 holder, const QcVillagerSettings &settings);
/*! The homeless \a villager (persistent id \a holder) moves into \a village: it counts there, a
    settlement without a style takes its style, and it claims the free bed nearest to \a near. */
void moveIn(QcVillage &village, QcVillager &villager, quint64 holder, const QcVec3 &near);

/*! Every villager of \a village is in the world (\a present, by persistent id): claims under
    other ids lapse. Returns the number of their holders. */
int dropLostClaims(QcVillage &village, const QSet<quint64> &present);

} // namespace QcVillagerHomes
