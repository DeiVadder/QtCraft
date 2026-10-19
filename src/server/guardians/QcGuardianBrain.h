#pragma once

#include "server/mobs/QcCreatureBrains.h"

// The brain of guardians (QcMobDefinition::Ai::Guardian, decision E-594; the sand golem): a mix of
// the reference's iron golem and its guardians. It keeps to the zone it guards (QcCreature::guards)
// and attacks only players who come close to it: the nearest player it sees within its sight range
// and within the kind's `guardLeash` of the zone, chased along a path and struck in melee. A player
// who leaves the leash is let go; without a target it strolls inside the zone and walks back into
// it when it stands outside. A guardian without a zone (summoned) is an ordinary hostile creature.
class QcGuardianBrain : public QcHostileBrain
{
public:
    // A guardian more than this many blocks outside its zone walks back into it.
    static constexpr double homeSlack = 1.0;

    using QcHostileBrain::QcHostileBrain;

    QcPhysics::Steering think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks) override;

private:
    // Turns a stroll that would leave the zone, or a guardian standing outside it, towards a random
    // cell of the zone (its centre from outside), with time to walk there at `walkSpeed`.
    void keepToZone(QcCreature &mob, const QcGuardPost &post, double walkSpeed);
};
