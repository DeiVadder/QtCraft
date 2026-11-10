// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/fortress/QcFortressPlanner.h"

#include "worldgen/nether/QcNetherComplexes.h"
#include "worldgen/nether/QcNetherTerrain.h"
#include "worldgen/terrain/QcNoise.h"

#include <algorithm>
#include <limits>

namespace {

using Plan = QcFortressPlan;
using Room = QcFortressPlan::Room;
using RoomKind = QcFortressPlan::RoomKind;
using Side = QcFortressPlan::Side;

// Cells between the net and the edge of its region.
constexpr int regionMargin = 12;
// A place needs at least this share of open lattice points: a fortress buried whole is no sight.
constexpr double leastOpenShare = 0.2;
// A pier every so many cells of a passage, two cells thick.
constexpr int pierEvery = 8;
// Every fortress has at least this many chests.
constexpr int leastChests = 2;
// The cells from a crossing's lane to its chest along the wall.
constexpr int chestBeside = 2;

int halfOf(RoomKind kind)
{
    switch (kind) {
    case RoomKind::Stairwell:
        return 6;
    case RoomKind::Platform:
        return 7;
    case RoomKind::Garden:
        return 5;
    case RoomKind::Crossing:
        break;
    }
    return 4;
}

QList<QcBlockPos> roomPiers(const Room &room)
{
    // Two by two under each corner, and under the middle of the long sides of a platform.
    QList<QcBlockPos> piers;
    const int far = room.half - 1;
    const QcBlockPos centre = room.centre.offset(0, -1, 0);
    for (const int x : {-room.half, far}) {
        for (const int z : {-room.half, far}) {
            for (int i = 0; i < 4; ++i) {
                piers.append(centre.offset(x + i % 2, 0, z + i / 2));
            }
        }
    }
    if (room.kind == RoomKind::Platform) {
        for (int i = -1; i <= 1; ++i) {
            piers.append(centre.offset(i, 0, -room.half));
            piers.append(centre.offset(i, 0, room.half));
            piers.append(centre.offset(-room.half, 0, i));
            piers.append(centre.offset(room.half, 0, i));
        }
    }
    return piers;
}

QList<QcBlockPos> passagePiers(const Plan::Passage &passage)
{
    // A stair's column reaches one cell below its floor.
    QList<QcBlockPos> piers;
    const int below = passage.rise > 0 ? 2 : 1;
    for (int along = pierEvery / 2; along + 1 < passage.length; along += pierEvery) {
        for (int lateral = -Plan::passageHalf; lateral <= Plan::passageHalf; ++lateral) {
            piers.append(passage.floorAt(along, lateral).offset(0, -below, 0));
            piers.append(passage.floorAt(along + 1, lateral).offset(0, -below, 0));
        }
    }
    return piers;
}

QcXzRect boundsOf(const QList<Room> &rooms, const QList<Plan::Passage> &passages)
{
    QcXzRect bounds{std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), std::numeric_limits<int>::min(), std::numeric_limits<int>::min()};
    const auto grow = [&bounds](const QcBlockBox &box) {
        bounds.minX = std::min(bounds.minX, box.low.x);
        bounds.minZ = std::min(bounds.minZ, box.low.z);
        bounds.maxX = std::max(bounds.maxX, box.high.x);
        bounds.maxZ = std::max(bounds.maxZ, box.high.z);
    };
    for (const Room &room : rooms) {
        grow(room.shell());
    }
    for (const Plan::Passage &passage : passages) {
        grow(passage.shell());
    }
    // The seals lie one cell out, and a chunk's own column test wants a cell of slack.
    return bounds.grown(2);
}

// Whether a passage leads into `room` on `side` on the floor at `floor`.
bool hasDoor(const Room &room, Side side, int floor, const QList<Plan::Passage> &passages)
{
    const QcBlockPos doorway = Plan::onSide({room.centre.x, floor, room.centre.z}, side, room.half + 1, 0);
    return std::ranges::any_of(passages, [&](const Plan::Passage &passage) {
        const bool starts = passage.side == side && passage.start == doorway;
        const bool ends = Plan::opposite(passage.side) == side && passage.floorAt(passage.length - 1, 0) == doorway;
        return starts || ends;
    });
}

// `room` with the doors of the passages that lead into it.
Room withDoors(Room room, const QList<Plan::Passage> &passages)
{
    for (const Side side : {Side::North, Side::East, Side::South, Side::West}) {
        const auto index = static_cast<std::size_t>(side);
        room.doors[index] = hasDoor(room, side, room.centre.y, passages);
        room.upperDoors[index] = room.kind == RoomKind::Stairwell && hasDoor(room, side, room.topFloor(), passages);
    }
    return room;
}

// The chest of a crossing: by the wall of its first side without a door, beside the lane and
// clear of the ribs in the corners, its front towards the middle.
Plan::Chest crossingChest(const Room &room)
{
    Side side = Side::North;
    for (const Side candidate : {Side::North, Side::East, Side::South, Side::West}) {
        if (!room.doors[static_cast<std::size_t>(candidate)]) {
            side = candidate;
            break;
        }
    }
    return {Plan::onSide(room.centre.offset(0, 1, 0), side, room.half - 1, chestBeside), Plan::opposite(side)};
}

QList<QcBlockPos> spawnersOf(const QList<Room> &rooms)
{
    QList<QcBlockPos> spawners;
    for (const Room &room : rooms) {
        if (room.kind == RoomKind::Platform) {
            spawners.append(room.centre.offset(0, 2, 0));
        }
    }
    return spawners;
}

} // namespace

QcFortressPlanner::QcFortressPlanner(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain, const QcNetherComplexes &complexes, quint64 seed)
    : m_definitions(definitions)
    , m_terrain(terrain)
    , m_complexes(complexes)
    , m_seed(qcMixSeed(seed, 0xf047))
{
}

double QcFortressPlanner::openShare(QcBlockPos corner) const
{
    int open = 0;
    const int lattice = settings().lattice;
    for (int i = 0; i < lattice * lattice; ++i) {
        const int x = corner.x + i % lattice * settings().spacing;
        const int z = corner.z + i / lattice * settings().spacing;
        open += m_terrain.isSolid(x, corner.y + 3, z) ? 0 : 1;
    }
    return static_cast<double>(open) / (lattice * lattice);
}

std::shared_ptr<const QcFortressPlan> QcFortressPlanner::plan(int regionX, int regionZ) const
{
    const QcNetherSettings::Fortress &fortress = settings();
    if (!enabled() || m_complexes.kindOf(regionX, regionZ) != QcNetherComplexes::Kind::Fortress) {
        return nullptr;
    }
    // Of a few places the one with the most open air around it; the parser keeps room for one.
    const int extent = (fortress.lattice - 1) * fortress.spacing;
    const int room = fortress.regionSize - extent - 2 * regionMargin;
    std::optional<QcBlockPos> best;
    double bestShare = leastOpenShare;
    for (int attempt = 1; attempt <= fortress.attempts; ++attempt) {
        const int x = regionX * fortress.regionSize + regionMargin + static_cast<int>(qcPositionRandom(seed(), regionX, attempt * 3, regionZ) * room);
        const int z = regionZ * fortress.regionSize + regionMargin + static_cast<int>(qcPositionRandom(seed(), regionX, attempt * 3 + 1, regionZ) * room);
        const double heights = fortress.highestFloor - fortress.lowestFloor + 1;
        const int y = fortress.lowestFloor + static_cast<int>(qcPositionRandom(seed(), regionX, attempt * 3 + 2, regionZ) * heights);
        const double share = openShare({x, y, z});
        if (share > bestShare) {
            bestShare = share;
            best = QcBlockPos{x, y, z};
        }
    }
    return best ? build(*best, {regionX, regionZ, seed() ^ 0xbe77}) : nullptr;
}

std::shared_ptr<const QcFortressPlan> QcFortressPlanner::planAt(QcBlockPos corner) const
{
    return build(corner, {0, 0, seed() ^ 0xde70});
}

std::shared_ptr<const QcFortressPlan> QcFortressPlanner::build(QcBlockPos corner, Rolls rolls) const
{
    int rolled = 0;
    const QcFortressNet::Roll roll = [&]() { return qcPositionRandom(rolls.seed, rolls.regionX, ++rolled, rolls.regionZ); };
    const QcNetherSettings::Fortress &fortress = settings();
    const QcFortressNet net = QcFortressNet::grow({fortress.lattice, fortress.minRooms, fortress.maxRooms, fortress.upperRooms, fortress.loopChance}, roll);

    auto plan = std::make_shared<QcFortressPlan>();
    plan->regionX = rolls.regionX;
    plan->regionZ = rolls.regionZ;
    plan->passages = passagesOf(net, corner);
    plan->rooms = roomsOf(net, corner, plan->passages);
    for (const Room &room : std::as_const(plan->rooms)) {
        const QList<QcBlockPos> piers = roomPiers(room);
        plan->parts.append(partBox(room.shell(), piers));
        plan->piers.append(piers);
    }
    for (const Plan::Passage &passage : std::as_const(plan->passages)) {
        const QList<QcBlockPos> piers = passagePiers(passage);
        plan->parts.append(partBox(passage.shell(), piers));
        plan->piers.append(piers);
    }
    plan->chests = chestsOf(plan->rooms, roll);
    plan->spawners = spawnersOf(plan->rooms);
    plan->bounds = boundsOf(plan->rooms, plan->passages);
    return plan;
}

double QcFortressPlanner::rockShare(const QcBlockBox &box, int step) const
{
    int rock = 0;
    int samples = 0;
    for (int z = box.low.z; z <= box.high.z; z += step) {
        for (int x = box.low.x; x <= box.high.x; x += step) {
            for (const int y : {box.low.y + 1, box.high.y - 1}) {
                rock += m_terrain.isSolid(x, y, z) ? 1 : 0;
                ++samples;
            }
        }
    }
    return samples > 0 ? static_cast<double>(rock) / samples : 0.0;
}

QcBlockBox QcFortressPlanner::partBox(const QcBlockBox &shell, const QList<QcBlockPos> &piers) const
{
    // Every fourth pier column down to its rock: the piers stand in groups of four or more.
    int lowest = shell.low.y;
    for (qsizetype i = 0; i < piers.size(); i += 4) {
        int y = piers[i].y;
        while (y > 1 && !m_terrain.isSolid(piers[i].x, y, piers[i].z)) {
            --y;
        }
        lowest = std::min(lowest, y);
    }
    return {{shell.low.x - 1, lowest, shell.low.z - 1}, {shell.high.x + 1, shell.high.y + 1, shell.high.z + 1}};
}

QList<QcFortressPlan::Room> QcFortressPlanner::roomsOf(const QcFortressNet &net, QcBlockPos corner, const QList<QcFortressPlan::Passage> &passages) const
{
    // One room per node; the upper node of the stairwell is its lower room.
    const QcNetherSettings::Fortress &fortress = settings();
    QList<Room> rooms;
    for (const QcFortressNet::Node &node : net.nodes()) {
        if (node.level == 1 && node.kind == RoomKind::Stairwell) {
            continue;
        }
        Room room;
        room.kind = node.kind;
        room.half = halfOf(node.kind);
        room.centre = corner.offset(node.x * fortress.spacing, node.level * Plan::levelRise, node.z * fortress.spacing);
        room.flight = net.flight();
        // A room under a room carries it; a platform stands open, the halls are always closed.
        const bool carries = node.level == 0 && net.nodeAt(node.x, node.z, 1).has_value();
        const bool hall = node.kind == RoomKind::Stairwell || node.kind == RoomKind::Garden;
        room.roofed = node.kind != RoomKind::Platform && (hall || carries || rockShare(room.shell(), 4) >= fortress.roofShare);
        rooms.append(withDoors(room, passages));
    }
    return rooms;
}

QList<QcFortressPlan::Passage> QcFortressPlanner::passagesOf(const QcFortressNet &net, QcBlockPos corner) const
{
    const QcNetherSettings::Fortress &fortress = settings();
    QList<Plan::Passage> passages;
    for (const QcFortressNet::Link &link : net.links()) {
        const QcFortressNet::Node &from = net.nodes()[link.from];
        const QcFortressNet::Node &to = net.nodes()[link.to];
        const Side side = net.sideBetween(link.from, link.to);
        const int fromHalf = halfOf(from.kind);
        const auto [stepX, stepZ] = Plan::stepOf(side);
        const QcBlockPos centre = corner.offset(from.x * fortress.spacing, from.level * Plan::levelRise, from.z * fortress.spacing);

        Plan::Passage passage;
        passage.side = side;
        passage.start = centre.offset(stepX * (fromHalf + 1), 0, stepZ * (fromHalf + 1));
        passage.length = fortress.spacing - fromHalf - halfOf(to.kind) - 1;
        passage.rise = (to.level - from.level) * Plan::levelRise;
        passage.roofed = passage.rise > 0 || rockShare(passage.shell(), 3) >= fortress.roofShare;
        passages.append(passage);
    }
    return passages;
}

QList<QcFortressPlan::Chest> QcFortressPlanner::chestsOf(const QList<QcFortressPlan::Room> &rooms, const QcFortressNet::Roll &roll) const
{
    QList<Plan::Chest> chests;
    QList<const Room *> empty;
    for (const Room &room : rooms) {
        if (room.kind == RoomKind::Garden) {
            chests.append({room.centre.offset(0, 1, 0), Side::South});
        }
    }
    for (const Room &room : rooms) {
        if (room.kind != RoomKind::Crossing) {
            continue;
        }
        if (roll() < settings().chestChance) {
            chests.append(crossingChest(room));
        } else {
            empty.append(&room);
        }
    }
    while (chests.size() < leastChests && !empty.isEmpty()) {
        chests.append(crossingChest(*empty.takeLast()));
    }
    return chests;
}
