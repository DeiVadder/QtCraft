#include "worldgen/mineshafts/QcMineshaftIncline.h"

#include "worldgen/mineshafts/QcMineshaftSite.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>

namespace {

// Blocks kept free of villages and surface structures around the tunnel where it runs near the
// surface, as around a ladder shaft.
constexpr int villageMargin = 8;
// The ground in front of the mouth lies at most this many blocks below the last step.
constexpr int mouthDrop = 2;
// The tunnel is two cells wide on each side of its middle lane with its walls, and a cell more
// around its mouth (the cleared box); the extent of the mine shaft keeps that inside the region.
constexpr int sideReach = 4;

// Unit steps of the sides north, east, south, west (QcMineshaftBlocks::sideOf).
constexpr std::array<std::pair<int, int>, 4> sideSteps{std::pair{0, -1}, {1, 0}, {0, 1}, {-1, 0}};

// A box of cells, inclusive corners.
struct Box
{
    QcBlockPos low;
    QcBlockPos high;

    bool meets(const Box &other) const
    {
        return low.x <= other.high.x && other.low.x <= high.x && low.y <= other.high.y && other.low.y <= high.y && low.z <= other.high.z
               && other.low.z <= high.z;
    }
};

Box boxOf(QcBlockPos a, QcBlockPos b)
{
    return {{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}, {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}};
}

// The open cells of a corridor (all three lanes, its full height).
Box boxOf(const QcMineshaftPlan::Corridor &corridor)
{
    const Box ends = boxOf(corridor.cell(0, -1), corridor.cell(corridor.length - 1, 1));
    return {ends.low, ends.high.offset(0, corridor.height() - 1, 0)};
}

// The open cells of a node.
Box boxOf(const QcMineshaftPlan::Node &node)
{
    return {node.centre.offset(-node.half, 0, -node.half), node.centre.offset(node.half, node.height - 1, node.half)};
}

// The cells of the tunnel's step `along` with its walls, floor and a row above.
Box shellOf(const QcMineshaftPlan::Corridor &tunnel, int along)
{
    const Box lanes = boxOf(tunnel.cell(along, -2), tunnel.cell(along, 2));
    return {lanes.low.offset(0, -1, 0), lanes.high.offset(0, tunnel.height(), 0)};
}

} // namespace

QcMineshaftIncline::QcMineshaftIncline(const QcTerrainGenerator &terrain, const QcMineshaftSettings &settings, const QcMineshaftBlocks &blocks)
    : m_terrain(terrain)
    , m_settings(settings)
    , m_blocks(blocks)
{
}

std::optional<QcMineshaftPlan::Entrance> QcMineshaftIncline::through(const QcMineshaftPlan &plan, QcMineshaftSite &site, Exit exit, bool rails) const
{
    const std::optional<QcMineshaftPlan::Corridor> tunnel = layout(plan, site, exit);
    if (!tunnel || !apart(plan, *tunnel, exit.node) || !clear(site, *tunnel)) {
        return std::nullopt;
    }
    return entrance(plan.nodes.at(exit.node), site, *tunnel, rails);
}

int QcMineshaftIncline::groundAt(QcMineshaftSite &site, QcBlockPos cell) const
{
    return m_terrain.groundTop(cell.x, cell.z, site.column(cell.x, cell.z));
}

std::optional<QcMineshaftPlan::Corridor> QcMineshaftIncline::layout(const QcMineshaftPlan &plan, QcMineshaftSite &site, Exit exit) const
{
    const QcBlockPos centre = plan.nodes.constFirst().centre;
    const QcMineshaftPlan::Node &room = plan.nodes.at(exit.node);
    const auto [dx, dz] = sideSteps.at(static_cast<std::size_t>(exit.side));
    QcMineshaftPlan::Corridor tunnel;
    tunnel.start = room.centre.offset(dx * (room.half + 1), 1, dz * (room.half + 1));
    tunnel.dx = dx;
    tunnel.dz = dz;
    tunnel.rise = 1;

    // Up while the step lies in the ground; the first cell above it is the mouth, still inside the
    // mine shaft's extent with the walls and the box cleared around it.
    for (int along = 0;; ++along) {
        const QcBlockPos feet = tunnel.cell(along);
        if (std::max(std::abs(feet.x - centre.x), std::abs(feet.z - centre.z)) + sideReach > m_settings.extent()) {
            return std::nullopt;
        }
        const int ground = groundAt(site, feet);
        // Its top row would open under a river or a valley floor below the sea.
        if (ground < feet.y + tunnel.height() - 1 && ground <= m_terrain.seaLevel()) {
            return std::nullopt;
        }
        if (feet.y <= ground) {
            continue;
        }
        if (along < 2 || ground <= m_terrain.seaLevel() || ground + 1 < feet.y - 1 - mouthDrop) {
            return std::nullopt;
        }
        tunnel.length = along;
        return tunnel;
    }
}

bool QcMineshaftIncline::apart(const QcMineshaftPlan &plan, const QcMineshaftPlan::Corridor &tunnel, int from) const
{
    QList<Box> taken;
    for (qsizetype i = 0; i < plan.nodes.size(); ++i) {
        if (i != from) {
            taken.append(boxOf(plan.nodes.at(i)));
        }
    }
    for (const QcMineshaftPlan::Corridor &corridor : plan.corridors) {
        taken.append(boxOf(corridor));
    }
    for (const QcMineshaftPlan::Room &room : plan.rooms) {
        const QcBlockPos origin = room.placement.origin();
        // Rooms are lower than a level (QcMineshaftSettings::fitsRoom).
        taken.append({origin, origin.offset(room.placement.sizeX() - 1, m_settings.levelSpacing, room.placement.sizeZ() - 1)});
    }
    for (int along = 0; along < tunnel.length; ++along) {
        const Box shell = shellOf(tunnel, along);
        if (std::ranges::any_of(taken, [&shell](const Box &box) { return box.meets(shell); })) {
            return false;
        }
    }
    return true;
}

bool QcMineshaftIncline::clear(QcMineshaftSite &site, const QcMineshaftPlan::Corridor &tunnel) const
{
    const Box all = boxOf(shellOf(tunnel, 0).low, shellOf(tunnel, tunnel.length - 1).high);
    if (!site.clearOfDungeons(all.low, all.high)) {
        return false;
    }
    const QcBlockPos mouth = tunnel.cell(tunnel.length);
    if (!site.awayFromSettlements(mouth.x, mouth.z, villageMargin)) {
        return false;
    }

    for (int along = 0; along < tunnel.length; ++along) {
        for (int lateral = -1; lateral <= 1; ++lateral) {
            if (!laneClear(site, tunnel.cell(along, lateral), tunnel.height())) {
                return false;
            }
        }
    }
    return true;
}

bool QcMineshaftIncline::laneClear(QcMineshaftSite &site, QcBlockPos lane, int height) const
{
    // In the ground (above it is open sky, no cave): no cave under the step or in the lane.
    const int ground = groundAt(site, lane);
    for (int dy = -1; dy < height && lane.y + dy <= ground; ++dy) {
        if (site.cave(lane.offset(0, dy, 0))) {
            return false;
        }
    }
    return ground >= lane.y + height + m_settings.groundCover || site.awayFromSettlements(lane.x, lane.z, villageMargin);
}

QcMineshaftPlan::Entrance QcMineshaftIncline::entrance(const QcMineshaftPlan::Node &room, QcMineshaftSite &site, const QcMineshaftPlan::Corridor &tunnel,
                                                       bool rails) const
{
    QcMineshaftPlan::Entrance entrance;
    entrance.kind = QcMineshaftPlan::Entrance::Kind::Incline;
    const QcBlockPos mouth = tunnel.cell(tunnel.length);
    entrance.top = {mouth.x, groundAt(site, mouth), mouth.z};
    entrance.out = tunnel.cell(tunnel.length - 1);
    for (int along = 0; along < tunnel.length; ++along) {
        for (int lateral = -1; lateral <= 1; ++lateral) {
            const QcBlockPos lane = tunnel.cell(along, lateral);
            entrance.lining.append(lane.offset(0, -1, 0));
            for (int dy = 0; dy < tunnel.height(); ++dy) {
                entrance.shaft.append(lane.offset(0, dy, 0));
            }
        }
    }

    // Supports every `supportSpacing` cells, counted from the mouth, which always has one.
    for (int along = (tunnel.length - 1) % m_settings.supportSpacing; along < tunnel.length; along += m_settings.supportSpacing) {
        for (const int lateral : {-1, 1}) {
            for (int dy = 0; dy < tunnel.height() - 1; ++dy) {
                entrance.blocks.append({tunnel.cell(along, lateral).offset(0, dy, 0), m_blocks.post});
            }
        }
        for (int lateral = -1; lateral <= 1; ++lateral) {
            entrance.blocks.append({tunnel.cell(along, lateral).offset(0, tunnel.height() - 1, 0), m_blocks.planks});
        }
    }

    // Rails from the node's edge up the middle lane, rising towards the mouth; level at the last
    // step unless the ground in front is one higher.
    if (rails) {
        const QcBlockId up = m_blocks.railsUp.at(static_cast<std::size_t>(QcMineshaftBlocks::sideOf(tunnel.dx, tunnel.dz)));
        const QcBlockId level = tunnel.dx != 0 ? m_blocks.railEastWest : m_blocks.railNorthSouth;
        entrance.blocks.append({room.centre.offset(tunnel.dx * room.half, 0, tunnel.dz * room.half), up});
        for (int along = 0; along < tunnel.length; ++along) {
            const bool last = along == tunnel.length - 1;
            entrance.blocks.append({tunnel.cell(along), !last || entrance.top.y == entrance.out.y ? up : level});
        }
    }
    return entrance;
}
