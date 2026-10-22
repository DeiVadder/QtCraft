#pragma once

#include "common/blockrules/QcRails.h"
#include "common/core/QcAabb.h"
#include "common/world/QcBlockAccess.h"
#include "server/entities/QcMinecart.h"

#include <QList>

// How minecarts meet each other and living bodies in a tick (decision E-382), worked out from the
// carts as they were before any of them moved, so the order of the table does not matter: carts that
// run into each other bounce along the line between them and pass most of their speed on, living
// bodies push carts away (a cart never moves a body), and after the carts moved, none of them ends
// deeper in a cart it overlapped, so carts pushed against a blocked cart stop instead of sinking into
// it. Carts that only touch leave each other be, so a crowd comes to rest.
namespace QcMinecartCollisions {

// Speed a body that walks into a cart gives it, metres per second.
inline constexpr double bodyPushSpeed = 1.5;
// Share of their speed against each other two carts keep when they bump (1 would bounce without loss).
inline constexpr double restitution = 0.5;

// Two awake carts whose bodies overlap, by table index (first < second), and the horizontal line from
// the first to the second. Carts standing (almost) on the same spot get the line along the track
// under the one with the lower id (east without a track), pointing from the lower id to the higher.
struct Pair
{
    qsizetype first = 0;
    qsizetype second = 0;
    QcVec3 line;
};

[[nodiscard]] QList<Pair> touching(const QList<QcMinecart> &carts, const QcRails::Table &rails, const QcBlockAccess &world);
// The changes of velocity the carts of `pairs` give each other and `bodies` (boxes of living bodies
// that ride no cart) give the carts, by table index.
[[nodiscard]] QList<QcVec3> pushes(const QList<QcMinecart> &carts, const QList<Pair> &pairs, const QList<QcAabb> &bodies);
// After the carts moved from `before` (their positions at the start of the tick, by table index):
// takes back as much of each cart's step as it needs so no pair of `pairs` ends closer along its line
// than it started, sharing the take-back between the carts that moved towards each other. A cart
// keeps the same share of its speed as of its step. Takes the carts and gives them back (moved in
// and out, no copy).
[[nodiscard]] QList<QcMinecart> keptApart(QList<QcMinecart> carts, const QList<Pair> &pairs, const QList<QcVec3> &before);

} // namespace QcMinecartCollisions
