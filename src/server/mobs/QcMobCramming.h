#pragma once

#include "server/mobs/QcMobManager.h"

#include <QList>

#include <optional>

// The damage of crammed cells (game rule maxEntityCramming, decision E-401): creatures in a cell
// with more than the limit are hurt while it still holds more (counting those that died this tick),
// each only in every `phases`-th tick, staggered by its id. QcServerCreatures adds the sounds
// and the loot; the measurements call it on the creatures alone.
namespace QcMobCramming {

// Damage a crammed creature takes whenever it is not invulnerable from its last hurt
// (QcMobManager::hurtInvulnerability), as in the reference.
inline constexpr double damage = 6.0;
// A crammed creature is hurt only in every this many ticks, staggered by its id: the reference's
// chance of one in four per tick, made deterministic. Deaths spread over a few ticks, so a crowd
// shrinks to about the limit instead of dying all at once.
inline constexpr int phases = 4;

// A creature cramming hurt: its kind, where it stood and its drops when it died.
struct Hurt
{
    quint32 id = 0;
    int type = 0;
    QcVec3 position;
    std::optional<QList<QcItemDrop>> drops;
};

// Hurts the crammed creatures whose turn it is in tick `tick` (the crowd as the last creature tick
// counted it) and returns them.
QList<Hurt> apply(QcMobManager &mobs, int limit, quint64 tick);

} // namespace QcMobCramming
