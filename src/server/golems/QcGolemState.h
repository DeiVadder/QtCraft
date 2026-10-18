#pragma once

#include <QtGlobal>

// What a golem (QcMobDefinition::Ai::Golem, decision E-471) keeps between ticks: whether a player
// built it (saved with the creature), the hostile creature it is after and the player it is angry
// with (not saved: a loaded golem looks again). A component of the creature (QcCreature::golem).
struct QcGolemState
{
    // Built by a player from blocks (QcGolemBuilding): it never attacks players.
    bool playerBuilt = false;
    // The creature it chases (entity id), 0 for none.
    quint32 prey = 0;
    // The player that hurt it (entity id) and the world time its anger ends (QcGolemGrudges).
    quint32 angryAt = 0;
    double angryUntil = -1.0;
};
