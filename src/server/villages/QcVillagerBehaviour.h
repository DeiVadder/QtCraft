// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcCreatureKindId.h"
#include "common/physics/QcPhysics.h"
#include "server/entities/QcCreature.h"
#include "server/mobbrains/QcMobBrain.h"
#include "server/pathing/QcDoorPassage.h"
#include "server/pathing/QcMobNavigator.h"
#include "server/pathing/QcPathTerrain.h"
#include "server/villages/QcVillagerHomes.h"
#include "server/villages/QcVillagerRoads.h"
#include "server/villages/QcVillagerTrades.h"

#include <QHash>
#include <QList>
#include <QRandomGenerator>

#include <functional>
#include <memory>

class QcContentRegistry;
class QcPathPlanner;
class QcServerWorld;
struct QcMobDefinition;
struct QcVillagePlan;

/*! \brief What villagers do (decision E-411): the brain of the villager AI
    (QcMobDefinition::Ai::Villager, decision E-423), called by QcMobBehaviour for creatures of a
    villager kind.

    Every decideTicks ticks (staggered, at most \c {thoughtsPerTick} villagers per tick) a
    villager checks its claims, claims a free bed and workstation, looks out for hostile
    creatures and picks its goal from the routine of its species or profession by the time of
    day: its workstation (farmers tend the ripe crops of every field around it), the meeting
    point, a stroll near home, its bed. In between it walks its path there (QcMobNavigator), on
    long walks in legs along the village roads (QcVillagerRoads), opens the wooden doors on its
    path and closes them behind it (QcDoorPassage), and lies down in its bed at night. It flees
    home from hostile creatures and whoever hurt it, and goes home when it rains. What it does to
    the world (doors, crops) waits in actions() for QcServerVillagers. */
class QcVillagerBehaviour : public QcMobBrain
{
public:
    /*! Ticks between two decisions of a villager. */
    static constexpr int decideTicks = 20;
    /*! How close a villager comes to its workstation, bed, a crop or a door to use it. */
    static constexpr double reach = 1.6;
    /*! Farmers look for ripe crops this far (horizontally) around their workstation. */
    static constexpr int cropRadius = 8;
    /*! A farmer that found no ripe crop looks again after this many decisions. */
    static constexpr int cropLookDecisions = 5;
    /*! Strolls go this far from home. */
    static constexpr double strollDistance = 8.0;
    /*! The meeting gathers within this many cells of the bell. */
    static constexpr int meetCloseEnough = 3;

    /*! \brief Something a villager does to the world, carried out by the server
        (QcServerVillagers). */
    struct Action
    {
        /*! \brief What it does. */
        enum class Kind : quint8 {
            /*! Opens the door at \c {pos}. */
            OpenDoor,
            /*! Closes the door at \c {pos}. */
            CloseDoor,
            /*! Harvests the crop at \c {pos}. */
            Harvest
        };
        /*! What it does. */
        Kind kind = Kind::OpenDoor;
        /*! The block it acts on. */
        QcBlockPos pos;
        /*! Entity id of the villager. */
        quint32 villager = 0;
    };

    /*! \brief Budgets per tick (QcServerLimits villagerThoughtsPerTick, villagerRoutesPerTick,
        villagerHarvestsPerTick). */
    struct Limits
    {
        /*! Villagers that decide in one tick. */
        int thoughtsPerTick = 64;
        /*! Routes searched in one tick. */
        int routesPerTick = 8;
        /*! Crops harvested in one tick. */
        int harvestsPerTick = 4;
    };

    /*! What a brain sees in a tick (QcMobBrain::Context). */
    using Context = QcMobBrain::Context;

    QcVillagerBehaviour(std::shared_ptr<const QcContentRegistry> content, QRandomGenerator &random, QcPathPlanner &planner);

    /*! Sets limits() to \a limits. */
    void setLimits(Limits limits) { m_limits = limits; }
    /*! Returns the budgets per tick. */
    const Limits &limits() const { return m_limits; }
    /*! Sets \a check, which tells whether it rains on the top of the column of a block
        (QcServerWeather::isRainingAt there); without one it never rains. */
    void setRainCheck(std::function<bool(QcBlockPos)> check) { m_rains = std::move(check); }
    /*! Returns whether it rains on the column of \a pos (setRainCheck()). */
    bool rainsAt(QcBlockPos pos) const { return m_rains && m_rains(pos); }
    /*! Returns the roads the villagers walk (QcVillagerRoads). */
    QcVillagerRoads &roads() { return m_roads; }
    /*! Returns the offers of villagers of the kind \a mobType, by their species (decisions E-421,
        E-414); they restock at their workstation during work. */
    const QcVillagerTrades &trades(QcCreatureKindId mobType) const;

    /*! A new tick: the budgets start again. */
    void startTick();
    /*! The sleeping villager \a mob stays in its bed (no physics) until the routine of the tick of
        \a context wakes it, it is hurt or its bed is gone; then it stands up beside the bed.
        Returns whether it still sleeps. */
    bool rest(QcCreature &mob, const Context &context) override;
    /*! One tick of the awake villager \a mob of the kind \a definition in the tick of \a context:
        decides when due (in a crowd every \a thinkTicks ticks; the budget of decisions per tick
        holds either way) and walks; returns its steering. */
    QcPhysics::Steering think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks) override;
    /*! Returns true: villagers still think in the slowest turn in a dense crowd and keep to their
        path in between. */
    bool thinksInIdleCrowd() const override { return true; }
    /*! The villager \a mob leaves the world for good (died, removed): the door it opened is
        closed behind it. One whose chunk unloads leaves its door as it is. */
    void leave(const QcCreature &mob) override;
    /*! Returns what the villagers did to the world since the last call. */
    QList<Action> takeActions() { return std::exchange(m_actions, {}); }
    /*! Returns the villagers that decided in the last tick (tests, measurements). */
    int thoughtsLastTick() const { return m_thoughts; }

private:
    // Claims, danger, the activity and the goal.
    void decide(QcCreature &mob, const Context &context);
    // A villager whose trading screen is open (QcVillager::trader) stands facing the player and
    // looks out for danger; false (the trade ends) once it must flee or it is time for bed.
    bool trade(QcCreature &mob, const Context &context);
    // Flees from the nearest hostile creature within fleeDistance (hostiles()).
    void lookOut(QcCreature &mob, const Context &context);
    // The nearest awake hostile creature within `fleeDistance` of `at`.
    std::optional<QcVec3> nearestHostile(const QcVec3 &at, double fleeDistance, const Context &context);
    // The awake hostile creatures of this tick by the column of 16 × 16 blocks they stand in,
    // gathered at the first look of the tick.
    const QHash<QcChunkPos, QList<QcVec3>> &hostiles(const Context &context);
    // The goal of the activity: a cell and how close counts as there; none to stand.
    struct Goal
    {
        QcBlockPos cell;
        int closeEnough = 1;
        QcMobPath::Purpose purpose = QcMobPath::Purpose::Errand;
    };
    std::optional<Goal> goalOf(QcCreature &mob, const Context &context);
    std::optional<Goal> strollGoal(const QcCreature &mob, const Context &context);
    // A farmer at work (its `profession` tends crops) keeps its crop or looks for one (not after
    // every decision while there is none); others and farmers that may not change the world
    // (QcCreature::mayGrief) drop it.
    void lookForCrops(QcCreature &mob, const QcVillagerProfession *profession, const Context &context) const;
    // A ripe crop within cropRadius of the farmer's workstation, if any: on the village's fields or
    // any other field, a player's too (decision E-490).
    std::optional<QcBlockPos> ripeCropNear(const QcVillager &villager, const Context &context) const;
    bool isRipe(QcBlockId block) const;
    // Walks to the goal, in legs when it lies far; flees when it must.
    QcPhysics::Steering walk(QcCreature &mob, const QcMobDefinition &definition, const Context &context);
    // The cell to search a path to now: the goal, or the leg towards it along the village roads, or
    // the way out of the water (QcMobLegs).
    QcBlockPos legTowards(QcCreature &mob, QcBlockPos target, const Context &context);
    // Lies down, harvests or faces its workstation once there.
    void arrive(QcCreature &mob, const Context &context);
    void lieDown(QcCreature &mob, const Context &context);

    std::shared_ptr<const QcContentRegistry> m_content;
    QRandomGenerator &m_random;
    QcPathPlanner &m_planner;
    QcVillagerRoads m_roads;
    // Villager species name -> its offers; m_noTrades for a creature kind without a species.
    QHash<QString, QcVillagerTrades> m_trades;
    QcVillagerTrades m_noTrades;
    Limits m_limits;
    std::function<bool(QcBlockPos)> m_rains;
    QList<Action> m_actions;
    // Ripe crop states, indexed by QcBlockId.
    std::vector<bool> m_ripe;
    // Turns at doors (decision E-423).
    QcDoorPassage m_doors;
    QHash<QcChunkPos, QList<QcVec3>> m_hostiles;
    bool m_hostilesGathered = false;
    int m_thoughts = 0;
    int m_routes = 0;
    int m_harvests = 0;
};
