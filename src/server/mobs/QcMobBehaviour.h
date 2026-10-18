#pragma once

#include "server/golems/QcGolemBrain.h"
#include "server/mobs/QcArcherBrain.h"
#include "server/mobs/QcCreatureBrains.h"
#include "server/mobs/QcFishSwimming.h"
#include "server/mobs/QcMobCrowd.h"
#include "server/mobs/QcMobManager.h"
#include "server/mobs/QcNeutralBrain.h"
#include "server/mobs/QcTameBrain.h"
#include "server/pathing/QcMobNavigator.h"
#include "server/pathing/QcPathPlanner.h"
#include "server/pathing/QcPathTerrain.h"
#include "server/villages/QcVillagerBehaviour.h"

#include <memory>
#include <utility>
#include <vector>

// What the awake creatures of QcMobManager do each tick (decision E-400): each thinks with the brain
// of its AI (QcMobDefinition::ai, decision E-423: QcPassiveBrain, QcHostileBrain, the villagers'
// QcVillagerBehaviour, QcArcherBrain, QcNeutralBrain, QcGolemBrain); flyers drift above the ground.
// Walkers follow their paths (QcMobNavigator) with the shared physics and are pushed apart by their
// crowd (QcMobCrowd), whose density also decides how much they think (QcMobCrowd::Tier); the path
// searches of all of them share a budget per tick (QcPathPlanner).
class QcMobBehaviour
{
public:
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
    // The brain of the creatures of kind `type`.
    QcMobBrain &brainOf(int type) const { return *m_brains[std::size_t(type)]; }

    // When a tick happens: the world's time and the tick's length in seconds.
    struct Clock
    {
        double worldTime = 0.0;
        double dt = 0.0;
    };

    // One tick of the awake creatures at the clock's world time: the waiting path searches, then each
    // decides and moves. Returns the blows they dealt to players; their shots wait in takeLaunches.
    QList<QcMobManager::PlayerHit> tick(QList<QcCreature> &mobs, QcServerWorld &world, const QList<QcMobManager::PlayerView> &players,
                                        Clock clock);
    // The shots of the creatures since the last call (QcMobBrain::Context::launches, decision E-425).
    QList<QcProjectileManager::Launch> takeLaunches() { return std::exchange(m_launches, {}); }
    // The blows of creatures at creatures since the last call (QcMobBrain::Context::creatureHits).
    QList<QcMobManager::CreatureHit> takeCreatureHits() { return std::exchange(m_creatureHits, {}); }
    // The blocks the creatures took and put down since the last call (decision E-460).
    QList<QcMobManager::BlockAction> takeBlockActions() { return std::exchange(m_blockActions, {}); }
    // Game rule mobGriefing (QcMobBrain::Context::griefing).
    void setGriefing(bool on) { m_griefing = on; }
    // A creature leaves the world for good (QcMobManager::Departure other than Unloaded): its brain
    // lets go of what it held (a villager's open door).
    void leave(const QcCreature &mob) const { brainOf(mob.type).leave(mob); }

private:
    // One tick of an awake creature in its tier.
    void update(qsizetype index, QcCreature &mob, const QcMobBrain::Context &context);
    // A creature without a mind (QcCreature::noAi, decision E-510): no brain, no steering, no blow;
    // only the physics, the crowd's push and leaving blocks move it.
    void withoutMind(qsizetype index, QcCreature &mob, const QcMobDefinition &definition, const QcServerWorld &world);
    // What a creature does without thinking in a crowd: along its path at its last speed without
    // searching and without stepping off an edge (`coasts`, throttled) or nothing (idle), and a blow
    // at a player in melee.
    QcPhysics::Steering withoutThinking(QcCreature &mob, const QcMobDefinition &definition, const QcMobBrain::Context &context,
                                        bool coasts);
    // Adds the push of the crowd to the steering.
    void steerApart(qsizetype index, const QcCreature &mob, const QcServerWorld &world, QcPhysics::Steering &steering) const;
    // One tick of the physics with the steering (none while knocked back).
    static void walk(QcCreature &mob, const QcMobDefinition &definition, const QcServerWorld &world, QcPhysics::Steering steering);
    // Drifting flight without gravity at a few metres above the ground.
    void fly(QcCreature &mob, const QcMobDefinition &definition, QcServerWorld &world, double dt);

    std::shared_ptr<const QcContentRegistry> m_content;
    QRandomGenerator &m_random;
    QcPathPlanner m_paths;
    // The brains use m_paths, so they come after it.
    QcVillagerBehaviour m_villagers;
    QcPassiveBrain m_passive;
    QcHostileBrain m_hostile;
    QcArcherBrain m_archers;
    QcGolemBrain m_golems;
    QcNeutralBrain m_neutral;
    // Tameable kinds think with these (decision E-480); wild ones through the brain of their AI.
    QcTameBrain m_tamePassive;
    QcTameBrain m_tameNeutral;
    // The brain and the path rules of every kind, indexed by type.
    std::vector<QcMobBrain *> m_brains;
    std::vector<QcPathRules> m_rules;
    QcMobCrowd m_crowd;
    // Water creatures (decision E-502).
    QcFishSwimming m_fish;
    TierCounts m_tiers;
    QList<QcProjectileManager::Launch> m_launches;
    QList<QcMobManager::CreatureHit> m_creatureHits;
    QList<QcMobManager::BlockAction> m_blockActions;
    bool m_griefing = true;
    // Ticks since the start: when creatures look for players and throttled ones think (by their ids).
    quint64 m_tick = 0;
};
