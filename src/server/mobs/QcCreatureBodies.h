#pragma once

#include "common/content/definitions/QcMobDefinition.h"
#include "common/physics/QcBodyGrid.h"
#include "server/entities/QcCreature.h"

#include <QList>

#include <limits>

// The creatures' push on players (decision E-882): the bodies of the awake creatures that push
// players (QcBodyPush::pushesPlayers) in a grid of columns (QcBodyGrid). Players move between the
// server's ticks, as their inputs arrive; the creatures stand still then, so the grid is built at
// most once between two ticks, when the first input needs it, and every input of every player
// looks only at the columns around him.
class QcCreatureBodies
{
public:
    // The push on a player with his feet at `feet` (a velocity, QcBodyPush::playerVelocity) from
    // `mobs` as they stand in server tick `tick`, with `generation` counting every creature added to
    // or removed from them (QcMobManager).
    [[nodiscard]] QcVec3 pushOnPlayer(const QcVec3 &feet, const QList<QcCreature> &mobs, const QList<QcMobDefinition> &definitions,
                                      quint64 tick, quint64 generation);

private:
    void build(const QList<QcCreature> &mobs, const QList<QcMobDefinition> &definitions);

    QcBodyGrid m_grid;
    // The tick and the generation of the creatures the grid was built for: a summon or a removal
    // between two ticks changes the generation and builds it anew.
    quint64 m_tick = std::numeric_limits<quint64>::max();
    quint64 m_generation = std::numeric_limits<quint64>::max();
};
