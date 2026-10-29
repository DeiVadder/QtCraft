// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/physics/QcBodyPush.h"
#include "server/golems/QcGolemBrain.h"
#include "server/guardians/QcGuardianBrain.h"
#include "server/mobbrains/QcArcherBrain.h"
#include "server/mobbrains/QcCreatureBrains.h"
#include "server/mobbrains/QcFishSwimming.h"
#include "server/mobbrains/QcNeutralBrain.h"
#include "server/mobbrains/QcTameBrain.h"
#include "server/mobs/QcMobCrowd.h"
#include "server/mobs/QcMobManager.h"
#include "server/pathing/QcMobNavigator.h"
#include "server/pathing/QcPathPlanner.h"
#include "server/pathing/QcPathTerrain.h"
#include "server/villages/QcVillagerBehaviour.h"

#include <memory>
#include <span>
#include <utility>
#include <vector>

// What the awake creatures of QcMobManager do each tick (decision E-400): each thinks with the brain
// of its AI (QcMobDefinition::ai, decision E-423: QcPassiveBrain, QcHostileBrain, the villagers'
// QcVillagerBehaviour, QcArcherBrain, QcNeutralBrain, QcGolemBrain, QcGuardianBrain); flyers drift above the ground,
// hoverers (QcMobHover) float after their brain's wish.
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
    QcMobBrain &brainOf(int type) const { return *m_brains[static_cast<std::size_t>(type)]; }

    // When a tick happens: the world's time and the tick's length in seconds.
    struct Clock
    {
        double worldTime = 0.0;
        double dt = 0.0;
    };

    // One tick of the awake creatures at the clock's world time: the waiting path searches, then each
    // decides and moves. Returns the blows they dealt to players; their shots wait in takeLaunches.
    QList<QcMobManager::PlayerHit> tick(std::span<QcCreature> mobs, QcServerWorld &world, const QList<QcMobManager::PlayerView> &players,
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
    // `steering` with the push of the crowd and of the players (decision E-882) added.
    [[nodiscard]] QcPhysics::Steering withCrowd(qsizetype index, const QcCreature &mob, const QcServerWorld &world,
                                                QcPhysics::Steering steering) const;
    // The steering with the factor of the creature's status effects on its speed (slowness,
    // decision E-838) and, for a diver in water, swimming up towards a chased player above it.
    [[nodiscard]] QcPhysics::Steering adjusted(const QcCreature &mob, const QcMobDefinition &definition, const QcMobBrain::Context &context,
                                               QcPhysics::Steering steering) const;
    // One tick of the physics with the steering (none while knocked back).
    static void walk(QcCreature &mob, const QcMobDefinition &definition, const QcServerWorld &world, QcPhysics::Steering steering);
    // The steering of the hoverer `index` with the push away from the hoverers it overlaps.
    [[nodiscard]] QcPhysics::Steering apart(qsizetype index, const QcCreature &mob, QcPhysics::Steering steering) const;
    // One tick of a hovering creature (QcMobHover, decision E-839) from its brain's steering: a child
    // with a mind that chases nobody orbits its parent; it turns from `yawBefore` towards where it
    // faces now at its turn speed.
    void hover(QcCreature &mob, const QcMobDefinition &definition, const QcMobBrain::Context &context, const QcPhysics::Steering &steering,
               float yawBefore) const;
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
    QcGuardianBrain m_guardians;
    // Tameable kinds think with these (decision E-480); wild ones through the brain of their AI.
    QcTameBrain m_tamePassive;
    QcTameBrain m_tameNeutral;
    // The brain and the path rules of every kind, indexed by type.
    std::vector<QcMobBrain *> m_brains;
    std::vector<QcPathRules> m_rules;
    QcMobCrowd m_crowd;
    // Hoverers push apart among themselves (decision E-839).
    QcMobCrowd m_hoverers{QcMobCrowd::Members::Hoverers};
    // Water creatures (decision E-502).
    QcFishSwimming m_fish;
    // The bodies of the players that push walkers in this tick (decision E-882): alive, not in a
    // minecart. Players are few, so each walker looks at all of them.
    std::vector<QcBodyPush::Body> m_pushers;
    TierCounts m_tiers;
    QList<QcProjectileManager::Launch> m_launches;
    QList<QcMobManager::CreatureHit> m_creatureHits;
    QList<QcMobManager::BlockAction> m_blockActions;
    bool m_griefing = true;
    // Ticks since the start: when creatures look for players and throttled ones think (by their ids).
    quint64 m_tick = 0;
};
