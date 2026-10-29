// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/mobs/QcCreatureBrains.h"

struct QcArcherState;
struct QcProjectileDefinition;
class QcContentRegistry;

// The brain of archers (QcMobDefinition::Ai::Archer, decision E-426): they pick a target like hostile
// creatures, walk towards it while it is out of sight or farther than `keepDistance`, back off from
// it within `minDistance` and circle it sideways in between. While they see it within `range` they
// draw for `drawSeconds` and shoot (a launch in QcMobBrain::Context::launches), aimed with gravity,
// drag and the lead of a moving target and spread by `spreadDegrees`, then wait `cooldownSeconds`
// before they draw again. Whatever launch speed the aim needs, an arrow does the damage of one at
// `speed` (Launch::damageFactor). A target in melee gets a blow besides (the archer is cornered);
// the blow breaks the draw. It shoots the first of its choices whose effect the target lacks and
// neither draws nor shoots while it drinks (decision E-623). Without a
// target they wander like passive ones.
class QcArcherBrain : public QcPassiveBrain
{
public:
    // Ticks a side of circling lasts at least; it changes earlier when the archer meets a wall.
    static constexpr int strafeTicks = 40;
    // Where an archer aims at a player: the middle of the body.
    static constexpr double aimHeight = 0.9;
    // How much of a new estimate of the target's velocity enters the kept one each tick.
    static constexpr double velocitySmoothing = 0.5;
    // The fastest a target is believed to move (m/s) when the lead of a shot is estimated.
    static constexpr double maxTargetSpeed = 15.0;
    // How far (m) a target may move beyond maxTargetSpeed between two looks before it counts as a
    // jump (a teleport) and its estimated velocity starts at rest again.
    static constexpr double targetJump = 3.0;
    // The longest flight an aim plans for, in ticks (aim).
    static constexpr int maxAimSteps = 200;

    QcArcherBrain(const QcContentRegistry &content, QRandomGenerator &random, QcPathPlanner &paths);

    QcPhysics::Steering think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks) override;

    // The launch velocity that brings a projectile of `definition` from `from` to `target`, moving
    // at `targetVelocity`, at about `speed` m/s: the flight time in whole ticks from the distance,
    // the lead along the target's velocity for that time, then the velocity whose flight under
    // gravity and drag (the ticks of QcProjectileFlight::step) ends there after those ticks. At most
    // QcProjectileDefinition::maxSpeed.
    static QcVec3 aim(const QcVec3 &from, const QcVec3 &target, const QcVec3 &targetVelocity, double speed,
                      const QcProjectileDefinition &definition);
    // `velocity` turned by up to `degrees` in a random direction.
    static QcVec3 spread(const QcVec3 &velocity, double degrees, QRandomGenerator &random);
    // What it shoots at `target`: the first of its choices whose effect the target lacks (the
    // witch's poison, decision E-623), else its projectile.
    static const QString &projectileFor(const QcArcherDefinition &archer, const QcMobManager::PlayerView &target);

protected:
    bool chases() const override { return true; }

private:
    // Draws while it sees the target within range, and shoots after the draw time.
    void drawAndShoot(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target, bool canShoot,
                      const Context &context);
    // Its way while it has a target: towards it, away from it, or around it.
    QcPhysics::Steering keepDistance(QcCreature &mob, const QcMobManager::PlayerView &target, bool sees, const Context &context,
                                     bool mayPlan);
    // Sideways around the target, `away` mixed in (backing off), never off an edge.
    QcPhysics::Steering circle(QcCreature &mob, const QcMobDefinition &definition, const QcVec3 &towards, double away,
                               const Context &context);

    const QcContentRegistry &m_content;
};
