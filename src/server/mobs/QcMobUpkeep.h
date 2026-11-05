// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/mobs/QcMobManager.h"

/*! \brief Rules that keep the creatures of QcMobManager in the world or take them out: despawning
    and leaving blocks a creature stands in. */
namespace QcMobUpkeep {

/*! A creature that is not kept and that no player came within this distance of for idleTicks (or
    struck in that time) vanishes by chance, in each tick with a chance of 1 in idleChance
    (decision E-836, as in the original game). \qcshared */
inline constexpr double idleDistance = 32.0;
inline constexpr int idleTicks = 600;
inline constexpr int idleChance = 800;

/*! \brief What becomes of a creature in this tick: it stays, it leaves the world, or it may
    leave by chance (1 in idleChance). */
enum class Fate : quint8 {
    /*! It stays. */
    Stays,
    /*! It leaves the world. */
    Despawns,
    /*! It may leave by chance. */
    MayDespawn
};

/*! \brief A creature's fate in this tick and its idle ticks after it. */
struct Upkeep
{
    /*! Its fate in this tick. */
    Fate fate = Fate::Stays;
    /*! Its idle ticks after it. */
    int idleTicks = 0;

    bool operator==(const Upkeep &) const = default;
};

/*! Returns the fate of \a mob of the kind \a definition among \a players in this tick (\a night:
    whether it is night).

    Only players with a body count (PlayerView::alive): spectators and the dead neither keep
    creatures nor drive them away, as in the original game. The idle ticks are 0 while such a
    player is within idleDistance or there is none, otherwise one more than before, at most
    idleTicks + 1. The creature leaves the world in the void (decision E-288), beyond
    QcMobManager::despawnDistance from every player, or as a night creature in daylight; it may
    leave once idle for longer than idleTicks, unless it guards a zone. Nobody is far away while
    nobody plays (decision E-353). */
Upkeep upkeepOf(const QcCreature &mob, const QcMobDefinition &definition, const QList<QcMobManager::PlayerView> &players, bool night);

/*! Moves \a mob, a walker of the kind \a definition whose body overlaps blocks of \a world, to the
    nearest clear place (QcBodyFit::clearFeet, decision E-352). With nowhere to go it stays and
    suffocates (QcEnvironmentDamage::applySuffocation). */
void leaveBlocks(QcCreature &mob, const QcMobDefinition &definition, const QcBlockAccess &world);

} // namespace QcMobUpkeep
