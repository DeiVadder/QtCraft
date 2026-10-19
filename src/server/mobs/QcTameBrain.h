#pragma once

#include "server/mobs/QcCreatureBrains.h"

struct QcTameDefinition;
struct QcTameState;

// The brain of tameable creatures (QcMobDefinition::tameable, decision E-480): a wild one thinks with
// the brain of its AI (`wild`: a wolf is neutral, a cat passive); a tame one belongs to its owner
// (QcTameState::owner, a player identity). It sits while told to; otherwise it attacks what hurt its
// owner or what its owner struck (QcTameDefinition::defends; never a pet, its owner's nor another player's),
// teleports next to its owner when far (only to a safe place in loaded chunks, never while confined),
// follows its owner when farther than the follow distance until close, and stands while its owner is
// offline.
class QcTameBrain : public QcPassiveBrain
{
public:
    // Seconds a pet keeps after a target before it gives up, and how far from it it gives up.
    static constexpr double targetSeconds = 20.0;
    static constexpr double targetRange = 24.0;
    // How far around its owner a pet teleports to (cells, horizontally) and how many places it tries.
    static constexpr double teleportRange = 3.0;
    static constexpr int teleportAttempts = 16;

    QcTameBrain(QRandomGenerator &random, QcPathPlanner &paths, QcMobBrain &wild);

    QcPhysics::Steering think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks) override;
    void leave(const QcCreature &mob) override { m_wild.leave(mob); }

    // Teleports a pet next to its owner at `owner`: a random cell within teleportRange around the
    // owner's, at most one cell up or down and not the owner's own or next to it, where it can stand
    // (QcSafeSpot::canStand: only loaded chunks), in teleportAttempts tries; false when none fits.
    static bool teleportNear(QcCreature &mob, const QcMobDefinition &definition, const QcBlockAccess &world, QRandomGenerator &random,
                             const QcVec3 &owner);
    // The owner of a tame creature among the players online and alive; nullptr while offline.
    static const QcMobManager::PlayerView *ownerOf(const QcTameState &tame, const Context &context);

private:
    // What a pet attacks for its owner (QcTameState::target, targetTick and targetTimer).
    struct Target
    {
        quint32 id = 0;
        quint64 tick = 0;
        double timer = 0.0;
    };
    // Takes up what hurt its owner or what its owner struck, keeps its target while it is valid;
    // `seconds` have passed since the last choice.
    [[nodiscard]] Target chosenTarget(const QcCreature &mob, const QcTameState &tame, const QcMobManager::PlayerView *owner,
                                      const Context &context, double seconds) const;
    // The creature with entity id `id` if a pet may attack it: there, awake, not this pet and no
    // pet at all (neither of the same owner nor of another player), within targetRange.
    static const QcCreature *validTarget(const QcCreature &mob, quint32 id, const Context &context);
    // Along a path to the target, or a blow in melee (QcMobBrain::Context::creatureHits).
    QcPhysics::Steering attack(QcCreature &mob, const QcMobDefinition &definition, const QcCreature &target, const Context &context,
                               bool mayPlan);
    // Keeps near its owner: teleports when far, follows when farther than the follow distance.
    QcPhysics::Steering keepNear(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &owner,
                                 const Context &context, bool mayPlan);

    QcMobBrain &m_wild;
};
