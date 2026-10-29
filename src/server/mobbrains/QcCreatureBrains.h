// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/mobbrains/QcMobBrain.h"
#include "server/pathing/QcMobNavigator.h"

#include <QRandomGenerator>

class QcPathPlanner;

// The brain of passive creatures (QcMobDefinition::Ai::Passive, decision E-423): they wander to
// reachable places nearby or stand, and flee from whoever hurt them to a reachable place away from
// them (decision E-400).
class QcPassiveBrain : public QcMobBrain
{
public:
    // Seconds a passive creature flees after a hit, and how far away it looks for a place to flee to.
    static constexpr double fleeSeconds = 4.0;
    static constexpr double fleeDistance = 10.0;
    // How far a wanderer looks for a place to walk to.
    static constexpr double wanderDistance = 8.0;

    QcPassiveBrain(QRandomGenerator &random, QcPathPlanner &paths);

    QcPhysics::Steering think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks) override;

protected:
    // Whether it looks for a player to chase.
    virtual bool chases() const { return false; }
    // Along the creature's path, asking for a new one only when it `mayPlan`.
    QcMobNavigator::Navigation navigate(QcCreature &mob, double speed, const Context &context, bool mayPlan);
    // Walking to a place nearby, standing still or fleeing when hurt.
    QcPhysics::Steering wander(QcCreature &mob, const QcMobDefinition &definition, const Context &context, bool mayPlan);
    QRandomGenerator &random() { return m_random; }
    const QcPathPlanner &paths() const { return m_paths; }
    // Along a path towards the target, or a blow in melee.
    QcPhysics::Steering chase(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target,
                              const Context &context, bool mayPlan);

private:
    QRandomGenerator &m_random;
    QcPathPlanner &m_paths;
};

// The brain of hostile creatures (QcMobDefinition::Ai::Hostile): they chase the nearest player they
// see (and for a few seconds one they lost sight of, decision E-401) along a path and strike it in
// melee with a line of sight; without a target they wander like passive ones.
class QcHostileBrain : public QcPassiveBrain
{
public:
    using QcPassiveBrain::QcPassiveBrain;

protected:
    bool chases() const override { return true; }
};
