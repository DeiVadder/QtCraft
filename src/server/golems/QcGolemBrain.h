// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/mobs/QcCreatureBrains.h"

// The brain of golems (QcMobDefinition::Ai::Golem, decision E-471): an iron golem guards the
// villagers. It hunts the nearest hostile creature it sees within its sight range and strikes it in
// melee, throwing it upwards (QcGolemDefinition::lift); it attacks a player only while it is angry
// with them (they hurt it) or while the village whose area it stands in holds a grudge against them
// (they hurt its villagers or golems, QcGolemGrudges), and never when a player built it. Without a
// target it strolls slowly like a passive creature and never flees; a village golem keeps its
// strolls within the area of its village and walks back to it when outside (the nearest village
// within homeReach; like the reference's stroll in the village and move back to the village).
class QcGolemBrain : public QcPassiveBrain
{
public:
    // It sees prey at most this far above or below its feet.
    static constexpr double verticalSight = 4.0;
    // A village golem outside every village walks back to the one whose centre lies within this
    // distance.
    static constexpr double homeReach = 64.0;

    using QcPassiveBrain::QcPassiveBrain;

    QcPhysics::Steering think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks) override;

    // The player the golem attacks this tick, nullptr for none: one it is angry with, else one the
    // village it stands in holds a grudge against, alive, targetable and within its sight range.
    static const QcMobManager::PlayerView *enemy(const QcCreature &mob, const QcMobDefinition &definition, const Context &context);
    // The hostile creature it hunts: its prey while that is awake and within sight, else, when it
    // `looks`, the nearest hostile creature it sees (nullptr: none).
    static const QcCreature *prey(QcCreature &mob, const QcMobDefinition &definition, const Context &context, bool looks);

private:
    // Turns a village golem's stroll back towards the centre of its village when it would leave the
    // village's area or stands outside it, with time to walk there at `walkSpeed`.
    static void keepToVillage(QcCreature &mob, const Context &context, double walkSpeed);
    // Walks along a path towards `target`, facing it.
    QcPhysics::Steering pursue(QcCreature &mob, const QcMobDefinition &definition, const QcVec3 &target, const Context &context,
                               bool mayPlan);
    // Whether a body at `position` is within the golem's reach and seen from its eyes.
    static bool inReach(const QcCreature &mob, const QcMobDefinition &definition, const QcVec3 &position, QcPhysics::BodySize body,
                        const QcBlockAccess &world);
    // Turns to `target` and strikes when its attack is ready; true when it struck.
    static bool readyToStrike(QcCreature &mob, const QcMobDefinition &definition, const QcVec3 &target);
};
