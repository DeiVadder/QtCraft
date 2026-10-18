#pragma once

#include "server/mobs/QcCreatureBrains.h"

struct QcGazeDefinition;

// The brain of neutral creatures (QcMobDefinition::Ai::Neutral, decision E-460; the enderman): they
// wander like passive ones until a player provokes them, then chase that player like hostile ones
// for angerSeconds, renewed while they see them. A player provokes one by hurting it (the player
// who struck or shot it; damage without a player provokes nobody) or, with a `gaze`, by looking at
// its head (gazedAt). One with `teleport` leaves water at once and now and then teleports next to
// a target that is far away; one that `carries` blocks takes one now and then while calm and puts
// it down again elsewhere (actions in QcMobBrain::Context::blockActions, only when it may change the
// world, QcCreature::mayGrief: the game rule mobGriefing on, not confined, decision E-510; otherwise
// a carried block is dropped as an item instead of put down).
class QcNeutralBrain : public QcPassiveBrain
{
public:
    // Seconds a provoked creature stays angry after it last saw its target.
    static constexpr double angerSeconds = 20.0;
    // A target farther than this (metres) may make an angry teleporter jump next to it.
    static constexpr double farTarget = 12.0;

    using QcPassiveBrain::QcPassiveBrain;

    QcPhysics::Steering think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks) override;

    // Whether `player` looks at a head at `head`: their look (QcMobManager::PlayerView::look) and the
    // direction to the head agree within the gaze's tolerance, which narrows with the distance d
    // (look · direction > 1 − tolerance / d, like the reference game), from no farther than its range,
    // and nothing hides it from their eyes (QcLineOfSight::seesPoint).
    static bool gazedAt(const QcMobManager::PlayerView &player, const QcVec3 &head, const QcGazeDefinition &gaze,
                        const QcBlockAccess &world);

private:
    // The player that provokes it now: one that looks at its head, or the player who struck it since
    // it last thought (QcCreature::struckBy) while that player is a valid target; nullptr for none.
    const QcMobManager::PlayerView *provoker(const QcCreature &mob, const QcMobDefinition &definition, const Context &context,
                                             bool looks) const;
    // Its target while angry, nullptr when it calmed down or the target is gone.
    static const QcMobManager::PlayerView *angryAt(QcCreature &mob, const Context &context);
    // Takes a block now and then while it carries none, puts it down (drops it without the game rule
    // mobGriefing) now and then while it does.
    void carry(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks);
    void takeBlock(const QcCreature &mob, const QcMobDefinition &definition, const Context &context);
    // Into the air beside or in front of it, on a full solid block, in reach of its eyes like taking.
    void placeBlock(const QcCreature &mob, const QcMobDefinition &definition, const Context &context);
};
