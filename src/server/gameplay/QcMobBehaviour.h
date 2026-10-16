#pragma once

#include "QcMobManager.h"
#include "QcMobCrowd.h"
#include "QcMobNavigator.h"
#include "QcPathPlanner.h"
#include "QcPathTerrain.h"
#include "QcVillagerBehaviour.h"

#include <memory>
#include <vector>

// What the awake creatures of QcMobManager do each tick (decision E-400): hostile ones chase the
// nearest player they see (and for a few seconds one they lost sight of, decision E-401) along a
// path and strike it in melee with a line of sight; the others wander to reachable places nearby,
// stand, or flee from whoever hurt them to a reachable place away from them; flyers drift above the
// ground. Walkers follow their paths (QcMobNavigator) with the shared physics and are pushed apart by
// their crowd (QcMobCrowd), whose density also decides how much they think (QcMobCrowd::Tier); the
// path searches of all of them share a budget per tick (QcPathPlanner).
class QcMobBehaviour
{
public:
    // Seconds a passive creature flees after a hit, and how far away it looks for a place to flee to.
    static constexpr double fleeSeconds = 4.0;
    static constexpr double fleeDistance = 10.0;
    // How far a wanderer looks for a place to walk to.
    static constexpr double wanderDistance = 8.0;

    QcMobBehaviour(std::shared_ptr<const QcContentRegistry> content, QRandomGenerator &random);

    void setPathLimits(qint64 nodesPerTick, qint64 nodesPerSearch, qint64 pathLength);
    const QcPathPlanner &paths() const { return m_paths; }
    // Thresholds of the AI tiers (QcServerLimits crowdThrottled, crowdIdle).
    void setCrowdLimits(qint64 throttled, qint64 idle) { m_crowd.setThresholds(throttled, idle); }
    // The crowds as the last tick counted them.
    const QcMobCrowd &crowd() const { return m_crowd; }
    // Creatures that thought fully, throttled and not at all in the last tick (tests, measurements).
    struct TierCounts
    {
        int full = 0;
        int throttled = 0;
        int idle = 0;
    };
    TierCounts tiersLastTick() const { return m_tiers; }

    // What villagers do (decision E-411).
    QcVillagerBehaviour &villagers() { return m_villagers; }
    const QcVillagerBehaviour &villagers() const { return m_villagers; }

    // When a tick happens: the world's time and the tick's length in seconds.
    struct Clock
    {
        double worldTime = 0.0;
        double dt = 0.0;
    };

    // One tick of the awake creatures at the clock's world time: the waiting path searches, then each
    // decides and moves.
    void tick(QList<QcCreature> &mobs, QcServerWorld &world, const QList<QcMobManager::PlayerView> &players, Clock clock,
              QList<QcMobManager::PlayerHit> &hits);

private:
    // What one tick of all creatures shares.
    struct Tick
    {
        QcServerWorld &world;
        const QList<QcMobManager::PlayerView> &players;
        double dt;
        QList<QcMobManager::PlayerHit> &hits;
        const QList<QcCreature> &mobs;
        double worldTime;
    };

    QcVillagerBehaviour::Context villagerContext(const QcCreature &mob, const Tick &tick) const
    {
        return {tick.world, tick.mobs, tick.worldTime, tick.dt, m_rules[std::size_t(mob.type)]};
    }

    // One tick of an awake creature in its tier.
    void update(qsizetype index, QcCreature &mob, const Tick &tick);
    // Decides what to do: a target, a path, melee. A throttled creature thinks every `thinkTicks`
    // ticks (1: every tick) and asks for no path.
    QcPhysics::Steering think(QcCreature &mob, const QcMobDefinition &definition, const Tick &tick, int thinkTicks);
    // What a creature does without thinking in a crowd: along its path at its last speed without
    // searching and without stepping off an edge (`coasts`, throttled) or nothing (idle), and a blow
    // at a player in melee.
    QcPhysics::Steering withoutThinking(QcCreature &mob, const QcMobDefinition &definition, const Tick &tick, bool coasts);
    // Adds the push of the crowd to the steering.
    void steerApart(qsizetype index, const QcCreature &mob, const QcServerWorld &world, QcPhysics::Steering &steering) const;
    // One tick of the physics with the steering (none while knocked back).
    static void walk(QcCreature &mob, const QcMobDefinition &definition, const QcServerWorld &world, QcPhysics::Steering steering);
    // Along a path towards the target, or a blow in melee.
    QcPhysics::Steering chase(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target,
                              const Tick &tick, bool mayPlan);
    // Along the creature's path, asking for a new one only when it `mayPlan`.
    QcMobNavigator::Navigation navigate(QcCreature &mob, double speed, const QcServerWorld &world, bool mayPlan);
    // Walking to a place nearby, standing still or fleeing when hurt.
    QcPhysics::Steering wander(QcCreature &mob, const QcMobDefinition &definition, const Tick &tick, bool mayPlan);
    // Drifting flight without gravity at a few metres above the ground.
    void fly(QcCreature &mob, const QcMobDefinition &definition, QcServerWorld &world, double dt);

    std::shared_ptr<const QcContentRegistry> m_content;
    QRandomGenerator &m_random;
    QcPathPlanner m_paths;
    // Uses m_paths, so it comes after it.
    QcVillagerBehaviour m_villagers;
    QcMobCrowd m_crowd;
    TierCounts m_tiers;
    // Ticks since the start: when creatures look for players and throttled ones think (by their ids).
    quint64 m_tick = 0;
    // Path rules of every kind, indexed by type.
    std::vector<QcPathRules> m_rules;
};
