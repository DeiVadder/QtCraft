// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/villages/QcVillagerDefinitions.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "server/pathing/QcMobLegs.h"

#include <QString>

#include <optional>

/*! \brief The state of one of a villager's offers (decision E-414), by the offer's id: how often
    it was traded since the last restock and how far demand raised its price. */
struct QcVillagerOffer
{
    /*! The highest demand a saved offer may have. */
    static constexpr int maxDemand = 64;

    /*! The offer's id (QcVillagerTrades). */
    QString id;
    /*! How often it was traded since the last restock. */
    int uses = 0;
    /*! How far demand raised its price. */
    int demand = 0;

    bool operator==(const QcVillagerOffer &) const = default;
};

/*! \brief What is saved of a villager (decisions E-411, E-414): the village it lives in, its bed
    (head) and workstation (claimed there under its creature's persistent id, QcCreature::uid,
    decision E-420), its profession with level and experience, the state of its offers and when
    it last restocked, the seed and village style of its look, the crops it brought in (food for
    breeding, stage 37.3) and when it last slept (decision E-470). */
struct QcVillagerRecord
{
    /*! The highest level. */
    static constexpr int maxLevel = 5;
    /*! The most experience a saved villager may have. */
    static constexpr int maxExperience = 1 << 20;
    /*! The most crops a saved villager may have brought in. */
    static constexpr int maxProduce = 1 << 16;
    /*! The most offers a saved villager may have. */
    static constexpr int maxOffers = 32;
    /*! The latest world time a restock may lie at (about 30 million years of play); a saved time
        beyond it is refused as damaged. */
    static constexpr double maxRestockTime = 1e15;

    /*! QcVillage::id; 0 for a villager without a village (summoned), which only strolls. */
    qint64 village = 0;
    /*! Its bed (the head), if it has one. */
    std::optional<QcBlockPos> home;
    /*! Its workstation, if it has one. */
    std::optional<QcBlockPos> work;
    /*! Its profession; empty for none. */
    QString profession;
    /*! Its trading level (1 to maxLevel). */
    int level = 1;
    /*! Its trading experience. */
    int experience = 0;
    /*! In the order of QcVillagerTrades::offersOf; aligned with it by QcVillagerTrades::sync. */
    QList<QcVillagerOffer> offers;
    /*! World time of the last restock (negative: never). */
    double lastRestock = -1.0;
    /*! The restocks on the day of lastRestock. */
    int restocks = 0;
    /*! The seed of its look. */
    quint32 look = 0;
    /*! The village style of its look. */
    QString style;
    /*! The crops it brought in (food for breeding). */
    int produce = 0;
    /*! World time it last lay in its bed (negative: never): villagers that slept within a day
        summon iron golems (QcVillageGolems, decision E-470). */
    double sleptAt = -1.0;

    bool operator==(const QcVillagerRecord &) const = default;
};

/*! \brief A villager as it lives in the world: its record and what it is doing (not saved; a
    villager loaded from its chunk decides anew).

    QcVillagerBehaviour moves it, QcServerVillagers carries out what it does to the world. */
struct QcVillager : QcVillagerRecord
{
    /*! What it is doing. */
    QcVillagerActivity activity = QcVillagerActivity::Wander;
    /*! A door it opened on its way and closes once it is through. */
    std::optional<QcBlockPos> openedDoor;
    /*! Where it is going (QcVillagerBehaviour::Goal) and how close counts as there; a stroll is
        searched like a wanderer's walk. None: it stands. \qcshared */
    std::optional<QcBlockPos> target;
    int closeEnough = 1;
    bool strolling = false;
    /*! At its target (QcVillagerBehaviour::arrive): it stands instead of walking on. */
    bool arrived = false;
    /*! On a long walk: the road cell it walks to first, and in water without a path to its
        target the road cell on land it gets out to first (QcMobLegs along the roads of
        QcVillagerRoads). */
    QcMobLegs::State legs;
    /*! Decisions until a farmer looks for ripe crops again after it found none
        (QcVillagerBehaviour::cropLookDecisions). */
    int cropLookIn = 0;
    /*! A ripe crop a farmer walks to. */
    std::optional<QcBlockPos> crop;
    /*! The place it runs from (a hostile creature) while the creature's fleeTimer runs. */
    std::optional<QcVec3> threat;
    /*! When it last panicked at a hostile creature nearby (world time, negative: not since it
        was loaded; not saved; decision E-470): a panicking villager among villagers that slept
        lately summons an iron golem (QcVillageGolems). */
    double panickedAt = -1.0;
    /*! The player (entity id) whose trading screen it has open, 0 for none (QcServerTrading),
        and where that player stands: it stands facing them while the screen is open. \qcshared
    */
    quint32 trader = 0;
    QcVec3 traderAt;
    /*! Ticks until it decides again what to do (QcVillagerBehaviour::decideTicks). */
    int decideIn = 0;
    /*! Its look changed (a new profession): clients get it again. */
    bool lookChanged = false;
};
