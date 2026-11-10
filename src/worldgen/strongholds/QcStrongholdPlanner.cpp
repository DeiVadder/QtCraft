// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/strongholds/QcStrongholdPlanner.h"

#include "common/core/QcWorldConstants.h"
#include "worldgen/strongholds/QcStrongholdRooms.h"
#include "worldgen/terrain/QcNoise.h"

#include <algorithm>
#include <array>
#include <functional>
#include <optional>

namespace {

using Plan = QcStrongholdPlan;
using Kind = Plan::Kind;
using Piece = Plan::Piece;
using Side = Plan::Side;
using Exit = Plan::Exit;

// A kind of piece with its weight, how many a stronghold has at most (0: any number) and how
// many pieces lie at least between it and the starting staircase: the original game's.
struct Weight
{
    Kind kind = Kind::Corridor;
    int weight = 0;
    int most = 0;
    int minDepth = 0;
};

constexpr std::array<Weight, 10> weights{{{Kind::Corridor, 40, 0, 0},
                                          {Kind::Prison, 5, 5, 0},
                                          {Kind::LeftTurn, 20, 0, 0},
                                          {Kind::RightTurn, 20, 0, 0},
                                          {Kind::Crossing, 10, 6, 0},
                                          {Kind::StraightStairs, 5, 5, 0},
                                          {Kind::SpiralStairs, 5, 5, 0},
                                          {Kind::ChestCorridor, 5, 4, 0},
                                          {Kind::Library, 10, 2, 5},
                                          {Kind::PortalRoom, 20, 1, 6}}};
// Pieces lie at most this many doorways from the starting staircase.
constexpr int maxDepth = 50;
// A stronghold is grown anew this often at most until it has a portal room.
constexpr int growAttempts = 24;
// Kinds tried for one doorway before it is walled up.
constexpr int kindTries = 5;
// The share of the libraries with two storeys, where they fit.
constexpr double tallShare = 0.5;

// Rolls in 0 to 1 from a seed, one after the other (splitmix64).
class Rolls
{
public:
    explicit Rolls(quint64 seed)
        : m_state(seed)
    {
    }

    [[nodiscard]] double next()
    {
        m_state += 0x9e3779b97f4a7c15ULL;
        quint64 z = m_state;
        z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
        z ^= z >> 31U;
        return static_cast<double>(z >> 11U) / 9007199254740992.0;
    }
    [[nodiscard]] int below(qsizetype count) { return std::min(static_cast<int>(next() * static_cast<double>(count)), static_cast<int>(count) - 1); }

private:
    quint64 m_state = 0;
};

// A doorway a further piece may lead on from: the piece and exit it leaves, the middle lowest
// cell of the new piece's doorway, the way it faces and how many doorways lie before it.
struct Pending
{
    int parent = 0;
    Exit exit = Exit::Forward;
    QcBlockPos door;
    Side facing = Side::North;
    int depth = 0;
};

// How a piece hangs on the stronghold: the piece and exit it leads on from (parent -1 for the
// starting staircase) and how many doorways lie before it.
struct Link
{
    int parent = -1;
    Exit exit = Exit::Forward;
    int depth = 0;
};

// The pieces of one stronghold as they grow from the starting staircase.
class Growth
{
public:
    Growth(const QcStrongholdSettings &settings, quint64 seed, QcBlockPos start)
        : m_settings(settings)
        , m_rolls(seed)
        , m_start(start)
    {
    }

    // Grows the pieces, the starting staircase first.
    void grow();
    // Adds the portal room growing left out (none at most `maxPieces`): at the deepest doorway
    // walled up where it fits, within the reach or else up to its depth beyond it; where none
    // fits, the newest pieces give way one by one.
    void addPortalRoom();
    [[nodiscard]] const QList<Piece> &pieces() const { return m_pieces; }
    [[nodiscard]] bool hasPortalRoom() const { return countOf(Kind::PortalRoom) > 0; }

private:
    void addStart();
    // Adds `piece` leading on from `link`'s exit and offers its doorways.
    void addPiece(const Piece &piece, Link link);
    // Removes the newest piece and walls its doorway in up again.
    void removeNewest();
    // Places a portal room at the deepest doorway walled up where it fits (`beyond`: up to its
    // own depth beyond the reach); returns whether it did.
    [[nodiscard]] bool placePortalRoom(bool beyond);
    // The doorways out of the piece `index`, `depth` doorways from the start.
    [[nodiscard]] QList<Pending> doorwaysOf(int index, int depth) const;
    [[nodiscard]] Pending takePending() { return m_pending.takeAt(rolls().below(m_pending.size())); }
    [[nodiscard]] bool hasPending() const { return !m_pending.isEmpty(); }
    [[nodiscard]] bool fits(const QcBlockBox &box, int reach) const;
    [[nodiscard]] bool allowed(Weight weight, int depth) const;
    [[nodiscard]] std::optional<Piece> pieceFor(const Pending &pending);
    [[nodiscard]] Plan::Door door();
    [[nodiscard]] int variantOf(Kind kind, bool tall);
    [[nodiscard]] int countOf(Kind kind) const { return m_counts[static_cast<std::size_t>(kind)]; }
    [[nodiscard]] QcBlockPos start() const { return m_start; }
    [[nodiscard]] Rolls &rolls() { return m_rolls; }

    const QcStrongholdSettings &m_settings;
    Rolls m_rolls;
    QcBlockPos m_start;
    QList<Piece> m_pieces; // NOLINT(qc-member-access): changed only in addStart(), addPiece() and removeNewest(), read through pieces()
    QList<Link> m_links; // NOLINT(qc-member-access): kept in step with m_pieces in the same three functions
    QList<Pending> m_pending; // NOLINT(qc-member-access): changed only in addPiece() and takePending(), read through hasPending()
    std::array<int, 11> m_counts{}; // NOLINT(qc-member-access): counted in addPiece() and removeNewest(), read through countOf()
};

void Growth::grow()
{
    addStart();
    while (hasPending() && pieces().size() < m_settings.maxPieces) {
        const Pending next = takePending();
        if (const std::optional<Piece> piece = pieceFor(next)) {
            addPiece(*piece, {next.parent, next.exit, next.depth});
        }
    }
}

void Growth::addStart()
{
    const Side facing = static_cast<Side>(rolls().below(4));
    const QcBlockBox box{start().offset(-2, 0, -2), start().offset(2, Plan::shapeOf(Kind::Start, false).height - 1, 2)};
    m_pieces.append({Kind::Start, facing, box});
    m_links.append(Link{});
    m_pending += doorwaysOf(0, 1);
}

void Growth::addPiece(const Piece &piece, Link link)
{
    m_pieces[link.parent].opened |= 1 << static_cast<int>(link.exit);
    m_pieces.append(piece);
    m_links.append(link);
    ++m_counts[static_cast<std::size_t>(piece.kind)];
    m_pending += doorwaysOf(static_cast<int>(m_pieces.size()) - 1, link.depth + 1);
}

void Growth::removeNewest()
{
    const Link link = m_links.takeLast();
    --m_counts[static_cast<std::size_t>(m_pieces.takeLast().kind)];
    m_pieces[link.parent].opened &= ~(1 << static_cast<int>(link.exit));
}

void Growth::addPortalRoom()
{
    while (!hasPortalRoom()) {
        const bool room = pieces().size() < m_settings.maxPieces;
        if (room && (placePortalRoom(false) || placePortalRoom(true))) {
            return;
        }
        // The starting staircase stays: a portal room always fits at its doorway once all other
        // pieces gave way.
        if (pieces().size() <= 1) {
            return;
        }
        removeNewest();
    }
}

bool Growth::placePortalRoom(bool beyond)
{
    QList<Pending> walled;
    for (int index = 0; index < pieces().size(); ++index) {
        const QList<Pending> doorways = doorwaysOf(index, m_links[index].depth + 1);
        for (const Pending &doorway : doorways) {
            if (!pieces()[index].opens(doorway.exit)) {
                walled.append(doorway);
            }
        }
    }
    std::ranges::stable_sort(walled, std::ranges::greater{}, &Pending::depth);
    // Beyond the reach by a portal room's depth at most: still inside the margin the generator
    // looks for strongholds within (QcStrongholdGenerator::extent()).
    const int reach = m_settings.reach + (beyond ? Plan::shapeOf(Kind::PortalRoom, false).depth + 1 : 0);
    for (const Pending &doorway : std::as_const(walled)) {
        const QcBlockBox box = Piece::placed(Kind::PortalRoom, false, doorway.facing, doorway.door);
        if (fits(box, reach)) {
            addPiece({Kind::PortalRoom, doorway.facing, box, door(), 0, 0}, {doorway.parent, doorway.exit, doorway.depth});
            return true;
        }
    }
    return false;
}

QList<Pending> Growth::doorwaysOf(int index, int depth) const
{
    QList<Pending> doorways;
    const Piece &piece = pieces()[index];
    const Plan::Shape shape = piece.shape();
    for (int i = 0; i < shape.exitCount; ++i) {
        const Plan::Doorway &doorway = shape.exits[static_cast<std::size_t>(i)];
        // A corridor has the doorways to its sides its variant names (bit 0 left, bit 1 right).
        if (piece.kind == Kind::Corridor && doorway.exit != Exit::Forward && (piece.variant & static_cast<int>(doorway.exit)) == 0) {
            continue;
        }
        const Side side = piece.sideOf(doorway.exit);
        const auto [stepX, stepZ] = QcFortressPlan::stepOf(side);
        doorways.append({index, doorway.exit, piece.at(doorway.x, doorway.y, doorway.z).offset(stepX, 0, stepZ), side, depth});
    }
    return doorways;
}

bool Growth::fits(const QcBlockBox &box, int reach) const
{
    const QcBlockPos from = start();
    const bool near = box.low.x >= from.x - reach && box.high.x <= from.x + reach && box.low.z >= from.z - reach && box.high.z <= from.z + reach;
    const bool high = box.low.y >= m_settings.lowestFloor && box.high.y < QcWorld::worldHeight;
    return near && high && std::ranges::none_of(pieces(), [&box](const Piece &piece) { return piece.box.intersects(box); });
}

bool Growth::allowed(Weight weight, int depth) const
{
    return (weight.most == 0 || countOf(weight.kind) < weight.most) && depth > weight.minDepth;
}

std::optional<Piece> Growth::pieceFor(const Pending &pending)
{
    if (pending.depth > maxDepth) {
        return std::nullopt;
    }
    int total = 0;
    for (const Weight &weight : weights) {
        total += allowed(weight, pending.depth) ? weight.weight : 0;
    }
    for (int attempt = 0; attempt < kindTries && total > 0; ++attempt) {
        int roll = rolls().below(total);
        const auto chosen = std::ranges::find_if(weights, [&](Weight weight) {
            const int share = allowed(weight, pending.depth) ? weight.weight : 0;
            roll -= share;
            return roll < 0;
        });
        bool tall = chosen->kind == Kind::Library && rolls().next() < tallShare;
        QcBlockBox box = Piece::placed(chosen->kind, tall, pending.facing, pending.door);
        if (tall && !fits(box, m_settings.reach)) {
            tall = false;
            box = Piece::placed(chosen->kind, tall, pending.facing, pending.door);
        }
        if (fits(box, m_settings.reach)) {
            return Piece{chosen->kind, pending.facing, box, door(), variantOf(chosen->kind, tall), 0};
        }
    }
    return std::nullopt;
}

Plan::Door Growth::door()
{
    // As in the original game: two in five open, the others a wooden door, bars or an iron door.
    static constexpr std::array<Plan::Door, 5> doors{Plan::Door::Opening, Plan::Door::Opening, Plan::Door::WoodDoor, Plan::Door::Grates, Plan::Door::IronDoor};
    return doors[static_cast<std::size_t>(rolls().below(doors.size()))];
}

int Growth::variantOf(Kind kind, bool tall)
{
    if (kind == Kind::Corridor) {
        return (rolls().next() < 0.5 ? 1 : 0) | (rolls().next() < 0.5 ? 2 : 0);
    }
    if (kind == Kind::Crossing) {
        return rolls().below(4);
    }
    return kind == Kind::Library && tall ? 1 : 0;
}

// The plan of `pieces` with the furnishing of its rooms; the eyes of its frames follow `eyeSeed`.
std::shared_ptr<const Plan> finished(int index, QList<Piece> pieces, quint64 eyeSeed, double eyeChance)
{
    auto plan = std::make_shared<Plan>();
    plan->index = index;
    plan->pieces = std::move(pieces);
    plan->bounds = {plan->pieces.constFirst().box.low.x, plan->pieces.constFirst().box.low.z, plan->pieces.constFirst().box.high.x,
                    plan->pieces.constFirst().box.high.z};
    for (const Piece &piece : std::as_const(plan->pieces)) {
        plan->bounds = {std::min(plan->bounds.minX, piece.box.low.x), std::min(plan->bounds.minZ, piece.box.low.z),
                        std::max(plan->bounds.maxX, piece.box.high.x), std::max(plan->bounds.maxZ, piece.box.high.z)};
        plan->chests += QcStrongholdRooms::chestsOf(piece);
        plan->frames += QcStrongholdRooms::framesOf(piece, eyeSeed, eyeChance);
        if (const std::optional<QcBlockPos> spawner = QcStrongholdRooms::spawnerOf(piece)) {
            plan->spawners.append(*spawner);
        }
    }
    return plan;
}

} // namespace

QcStrongholdPlanner::QcStrongholdPlanner(const QcStrongholdSettings &settings, Ground ground, quint64 seed)
    : m_settings(settings)
    , m_ground(std::move(ground))
    , m_seed(qcMixSeed(seed, 0x5770))
{
}

std::shared_ptr<const QcStrongholdPlan> QcStrongholdPlanner::plan(int index, QcBlockPos at) const
{
    // The floor from the seed, low enough that ground covers the staircase.
    const int span = settings().maxStartFloor - settings().minStartFloor + 1;
    const int drawn = settings().minStartFloor + std::min(span - 1, static_cast<int>(qcPositionRandom(seed(), index, 0, 7) * span));
    const int covered = m_ground(at.x, at.z) - settings().groundCover - (Plan::shapeOf(Plan::Kind::Start, false).height - 1);
    return planAt(index, {at.x, std::max(settings().lowestFloor, std::min(drawn, covered)), at.z});
}

std::shared_ptr<const QcStrongholdPlan> QcStrongholdPlanner::planAt(int index, QcBlockPos at) const
{
    const auto grownAt = [&](int attempt) {
        Growth growth(settings(), qcMixSeed(seed(), static_cast<quint64>(index) * 64U + static_cast<quint64>(attempt)), at);
        growth.grow();
        return growth;
    };
    const auto grown = [&] {
        for (int attempt = 0; attempt + 1 < growAttempts; ++attempt) {
            Growth growth = grownAt(attempt);
            if (growth.hasPortalRoom()) {
                return growth;
            }
        }
        return grownAt(growAttempts - 1);
    };
    Growth growth = grown();
    // Every stronghold has its portal room: the last attempt gets one where it fits.
    growth.addPortalRoom();
    return finished(index, growth.pieces(), qcMixSeed(seed(), 0xe7e), settings().eyeChance);
}
