#include "server/minecarts/QcMinecartCollisions.h"

#include "common/physics/QcPhysics.h"
#include "server/minecarts/QcMinecartManager.h"
#include "server/minecarts/QcMinecartMotion.h"

#include <QHash>

#include <algorithm>
#include <ranges>

namespace QcMinecartCollisions {

namespace {

// Rounds that share the take-back between the carts of a pair; a chain of carts pushed against a
// blocked one needs about one round per cart. Whatever still closes in after them stays where it was.
constexpr int sharedRounds = 16;
// Closer than this along their line counts as not closer (rounding).
constexpr double closingTolerance = 1e-9;

qint64 columnKey(int x, int z)
{
    return (qint64(x) << 32) ^ qint64(quint32(z));
}

QcVec3 centreOf(const QcAabb &box)
{
    return (box.min + box.max) * 0.5;
}

// The horizontal direction from `from` to `to`; nothing when they are (almost) above each other.
std::optional<QcVec3> horizontalDirection(const QcVec3 &from, const QcVec3 &to)
{
    const QcVec3 delta{to.x - from.x, 0.0, to.z - from.z};
    if (delta.length() < 1e-6) {
        return std::nullopt;
    }
    return delta.normalized();
}

// The line from `cart` to `other` when they stand on the same spot: along the track under the one
// with the lower id (east without a track), from the lower id to the higher.
QcVec3 sameSpotLine(const QcMinecart &cart, const QcMinecart &other, const QcRails::Table &rails, const QcBlockAccess &world)
{
    const QcMinecart &lower = cart.id < other.id ? cart : other;
    const std::optional<QcMinecartMotion::Segment> segment = QcMinecartMotion::segmentAt(lower.motion.position, rails, world);
    const QcVec3 level = segment ? QcVec3{segment->direction.x, 0.0, segment->direction.z} : QcVec3{};
    const QcVec3 along = level.length() > 1e-6 ? level.normalized() : QcVec3{1.0, 0.0, 0.0};
    return cart.id < other.id ? along : along * -1.0;
}

// How far each cart has come along the line of `pair` towards the other, with its step scaled by `share`.
std::pair<double, double> approaches(const QList<QcMinecart> &carts, const Pair &pair, const QList<QcVec3> &before, const QList<double> &share)
{
    const QcVec3 first = (carts[pair.first].motion.position - before[pair.first]) * share[pair.first];
    const QcVec3 second = (carts[pair.second].motion.position - before[pair.second]) * share[pair.second];
    return {first.dot(pair.line), -second.dot(pair.line)};
}

// One round of take-backs: every cart keeps the smallest share of its step that any of its pairs
// allows with the shares of the round before (all pairs at once, so the order does not matter).
// `shared` splits a pair's take-back between both carts by how far each came; without it every cart
// that came closer stays where it was. Returns whether a share changed.
bool takeBack(const QList<QcMinecart> &carts, const QList<Pair> &pairs, const QList<QcVec3> &before, QList<double> &share, bool shared)
{
    QList<double> next = share;
    bool changed = false;
    for (const Pair &pair : pairs) {
        const auto [first, second] = approaches(carts, pair, before, share);
        const double closing = first + second;
        if (closing <= closingTolerance) {
            continue;
        }
        const double forward = std::max(first, 0.0);
        const double backward = std::max(second, 0.0);
        const auto limit = [&](qsizetype index, double came) {
            if (came <= 0.0) {
                return;
            }
            const double kept = shared ? (came - closing * came / (forward + backward)) / came : 0.0;
            next[index] = std::min(next[index], share[index] * std::max(kept, 0.0));
            changed = true;
        };
        limit(pair.first, forward);
        limit(pair.second, backward);
    }
    share = next;
    return changed;
}

} // namespace

QList<Pair> touching(const QList<QcMinecart> &carts, const QcRails::Table &rails, const QcBlockAccess &world)
{
    // Awake carts by the column of blocks they stand in; carts only touch carts in the columns around.
    QHash<qint64, QList<qsizetype>> columns;
    for (qsizetype i = 0; i < carts.size(); ++i) {
        if (!carts[i].dormant) {
            const QcBlockPos at = QcPhysics::blockContaining(carts[i].motion.position);
            columns[columnKey(at.x, at.z)].append(i);
        }
    }

    QList<Pair> pairs;
    for (qsizetype i = 0; i < carts.size(); ++i) {
        if (carts[i].dormant) {
            continue;
        }
        const QcAabb box = QcMinecartManager::bodyOf(carts[i]);
        const QcBlockPos at = QcPhysics::blockContaining(carts[i].motion.position);
        const auto overlapping = [&](qsizetype j) { return j > i && box.intersects(QcMinecartManager::bodyOf(carts[j])); };
        for (int column = 0; column < 9; ++column) {
            const QList<qsizetype> near = columns.value(columnKey(at.x + column % 3 - 1, at.z + column / 3 - 1));
            for (const qsizetype j : near | std::views::filter(overlapping)) {
                const std::optional<QcVec3> line = horizontalDirection(carts[i].motion.position, carts[j].motion.position);
                pairs.append({i, j, line ? *line : sameSpotLine(carts[i], carts[j], rails, world)});
            }
        }
    }
    return pairs;
}

QList<QcVec3> pushes(const QList<QcMinecart> &carts, const QList<Pair> &pairs, const QList<QcAabb> &bodies)
{
    QList<QcVec3> change(carts.size());
    for (const Pair &pair : pairs) {
        const double mine = carts[pair.first].motion.velocity.dot(pair.line);
        const double theirs = carts[pair.second].motion.velocity.dot(pair.line);
        // Approaching carts of equal mass bounce off each other along the line between them, losing
        // part of their speed (restitution); carts that touch without approaching leave each other be,
        // so a crowd of carts comes to rest.
        const double exchange = mine > theirs ? (1.0 + restitution) * 0.5 * (mine - theirs) : 0.0;
        change[pair.first] = change[pair.first] - pair.line * exchange;
        change[pair.second] = change[pair.second] + pair.line * exchange;
    }

    for (qsizetype i = 0; i < carts.size(); ++i) {
        const QcMinecart &cart = carts[i];
        if (cart.dormant) {
            continue;
        }
        const QcAabb box = QcMinecartManager::bodyOf(cart);
        for (const QcAabb &body : bodies) {
            const std::optional<QcVec3> away = box.intersects(body) ? horizontalDirection(centreOf(body), cart.motion.position) : std::nullopt;
            if (away) {
                change[i] = change[i] + *away * std::max(0.0, bodyPushSpeed - cart.motion.velocity.dot(*away));
            }
        }
    }
    return change;
}

void keepApart(QList<QcMinecart> &carts, const QList<Pair> &pairs, const QList<QcVec3> &before)
{
    QList<double> share(carts.size(), 1.0);
    bool changed = true;
    for (int round = 0; round < sharedRounds && changed; ++round) {
        changed = takeBack(carts, pairs, before, share, true);
    }
    // Shares only shrink and every round without sharing stops at least one more cart, so this ends.
    while (changed) {
        changed = takeBack(carts, pairs, before, share, false);
    }

    for (qsizetype i = 0; i < carts.size(); ++i) {
        if (share[i] < 1.0) {
            QcMinecart &cart = carts[i];
            cart.motion.position = before[i] + (cart.motion.position - before[i]) * share[i];
            cart.motion.velocity = cart.motion.velocity * share[i];
        }
    }
}

} // namespace QcMinecartCollisions
