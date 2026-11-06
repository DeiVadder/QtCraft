// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/mobbrains/QcCreatureBrains.h"

struct QcGazeDefinition;

/*! \brief The brain of neutral creatures (QcMobDefinition::Ai::Neutral, decision E-460; the
    shadewalker): they wander like passive ones until a player provokes them, then chase that player
    like hostile ones for QcMobDefinition::angerSeconds, renewed while they see them. One that
    barters (decision E-1081) also chases who wears none of what spares him while it sees him and
    no loved item draws it away, stands while it looks at an item and is never angry while young.

    A player provokes one by hurting it (the player who struck or shot it; damage without a
    player provokes nobody) or, with a \c {gaze}, by looking at its head (gazedAt). One with
    \c {teleport} leaves water at once and now and then teleports next to a target that is far
    away; one that \c {carries} blocks takes one now and then while calm and puts it down again
    elsewhere (actions in QcMobBrain::Context::blockActions, only when it may change the world,
    QcCreature::mayGrief: the game rule mobGriefing on, not confined, decision E-510; otherwise a
    carried block is dropped as an item instead of put down). */
class QcNeutralBrain : public QcPassiveBrain
{
public:
    /*! A target farther than this (metres) may make an angry teleporter jump next to it. */
    static constexpr double farTarget = 12.0;

    using QcPassiveBrain::QcPassiveBrain;

    QcPhysics::Steering think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks) override;

    /*! Returns whether \a player looks at a head at \a head in \a world: their look
        (QcMobManager::PlayerView::look) and the direction to the head agree within \a gaze's
        tolerance, which narrows with the distance d (look · direction > 1 − tolerance / d, like
        the reference game), from no farther than its range, and nothing hides it from their eyes
        (QcLineOfSight::seesPoint). */
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
