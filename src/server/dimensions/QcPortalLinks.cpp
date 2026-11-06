// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/dimensions/QcPortalLinks.h"

#include "common/core/QcWorldConstants.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/chunks/QcChunkStreaming.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

using Axis = QcPortalFrame::Axis;

// The cell beside the frame `step` blocks away, across its plane (0: the frame's own cell).
QcBlockPos beside(const QcPortalFrame &frame, int across, int up, int step)
{
    const QcBlockPos cell = frame.at(across, up);
    return frame.axis == Axis::X ? cell.offset(0, 0, step) : cell.offset(step, 0, 0);
}

int floorOf(double value)
{
    return static_cast<int>(std::floor(value));
}

// The highest inner floor a new frame of `height` may have in `dimension`: below its ceiling and
// below the top of the world.
int highestFloor(const QcDimensionDefinition &dimension, int height)
{
    const int top = dimension.ceiling > 0 ? dimension.ceiling - 1 : QcWorld::worldHeight - 1;
    return top - height - 1;
}

// The lowest floor of a frame in `dimension`, whose highest is `highest`.
int lowestFloor(const QcDimensionDefinition &dimension, int highest)
{
    return std::min(dimension.portalFloor, highest);
}

// Looks for ground where a new frame stands without building more than itself.
class SpotSearch
{
public:
    // With `depth` 1 the cells in front of and behind the frame count, with 0 only its plane.
    SpotSearch(const QcServerWorld &world, const QcPortalFrame &shape, int highest, int depth)
        : m_world(world)
        , m_content(world.content())
        , m_shape(shape)
        , m_highest(highest)
        , m_depth(depth)
    {
    }

    // The frame nearest `point` within `radius` columns; none without one.
    std::optional<QcPortalFrame> nearest(const QcVec3 &point, int radius) const
    {
        std::optional<QcPortalFrame> best;
        double bestDistance = 0.0;
        const QcBlockPos centre{floorOf(point.x), floorOf(point.y), floorOf(point.z)};
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                const std::optional<QcPortalFrame> found = inColumn(centre.offset(dx, 0, dz));
                const double distance = found ? (found->arrival() - point).length() : 0.0;
                if (found && (!best || distance < bestDistance)) {
                    best = found;
                    bestDistance = distance;
                }
            }
        }
        return best;
    }

private:
    // The frame with its lowest inner cell in the column of `cell`, nearest its height; the whole
    // column from 2 to the highest floor, alternately above and below.
    std::optional<QcPortalFrame> inColumn(QcBlockPos cell) const
    {
        const int reach = std::max(cell.y - 2, m_highest - cell.y);
        for (int step = 0; step <= 2 * reach; ++step) {
            const int y = cell.y + (step % 2 == 0 ? step / 2 : -(step + 1) / 2);
            if (y < 2 || y > m_highest) {
                continue;
            }
            QcPortalFrame frame = m_shape;
            frame.origin = {cell.x, y, cell.z};
            if (fits(frame)) {
                return frame;
            }
        }
        return std::nullopt;
    }

    bool isFree(QcBlockPos pos) const
    {
        const std::optional<QcBlockId> block = m_world.blockAt(pos);
        if (!block || !m_content.isValidBlock(*block)) {
            return false;
        }
        const QcBlockDefinition &definition = m_content.block(*block);
        return *block == qcAirBlockId || (definition.replaceable && !definition.liquid && !definition.fire.isFire);
    }

    bool isGround(QcBlockPos pos) const
    {
        const std::optional<QcBlockId> block = m_world.blockAt(pos);
        if (!block || !m_content.isValidBlock(*block)) {
            return false;
        }

        // Not on what hurts, such as the magma around the pools of the deltas.
        const QcBlockDefinition &definition = m_content.block(*block);
        return definition.solid && !definition.liquid && !definition.keepsSpawnsAway();
    }

    // Ground under the frame and room for it; with `m_depth` 1 also under and above the cells one
    // step in front of and behind it, so that one steps out onto ground (decision E-1025).
    bool fits(const QcPortalFrame &frame) const
    {
        for (int step = -m_depth; step <= m_depth; ++step) {
            if (!rowFits(frame, step)) {
                return false;
            }
        }
        return true;
    }

    // Ground under the row of cells `step` beside the frame (0: its plane) and room above it.
    bool rowFits(const QcPortalFrame &frame, int step) const
    {
        for (int across = -1; across <= frame.width; ++across) {
            if (!isGround(beside(frame, across, -2, step))) {
                return false;
            }
            for (int up = -1; up <= frame.height; ++up) {
                if (!isFree(beside(frame, across, up, step))) {
                    return false;
                }
            }
        }
        return true;
    }

    const QcServerWorld &m_world;
    const QcContentRegistry &m_content;
    QcPortalFrame m_shape;
    int m_highest = 0;
    int m_depth = 0;
};

} // namespace

QcPortalLinks::QcPortalLinks(QcServerState &state, QcServerBlocks &blocks)
    : m_state(state)
    , m_blocks(blocks)
{
}

std::optional<QcPortalLinks::Destination> QcPortalLinks::destination(const QcPortalDefinition &portal, const QcVec3 &position)
{
    const QString here = m_state.current().name();
    const bool outward = here == portal.from;
    if (!outward && here != portal.to) {
        return std::nullopt;
    }
    if (!outward && portal.back == QcPortalDefinition::Back::Spawn) {
        QcServerDimension &overworld = m_state.dimensions().overworld();
        const QcServerState::Scope scope(m_state, overworld);
        return Destination{&overworld, m_state.spawnPoint().position()};
    }

    const QcResult<QcServerDimension *> opened = m_state.openDimension(outward ? portal.to : portal.from);
    if (!opened) {
        qCWarning(lcServer) << "The portal" << portal.name << "leads nowhere:" << opened.error();
        return std::nullopt;
    }
    QcServerDimension &target = **opened;
    const std::optional<QcDimensionDefinition::Arrival> &arrival = target.definition().arrival;
    if (arrival && portal.arrivalIn(target.definition(), here) == QcPortalDefinition::Arrival::Fixed) {
        return Destination{&target, fixed(target, *arrival)};
    }
    return Destination{&target, scaled(portal, target, position)};
}

QcVec3 QcPortalLinks::scaled(const QcPortalDefinition &portal, QcServerDimension &target, const QcVec3 &position)
{
    const double scale = m_state.current().definition().coordinateScale / target.definition().coordinateScale;
    const QcServerState::Scope scope(m_state, target);
    const int highest = highestFloor(target.definition(), QcPortalDefinition::innerOf(portal.minHeight));
    const double lowest = lowestFloor(target.definition(), highest);
    const QcVec3 point{position.x * scale, std::clamp(position.y, lowest, static_cast<double>(highest)), position.z * scale};
    const double radius = std::max(1.0, portal.searchRadius / target.definition().coordinateScale);
    if (const std::optional<QcPortalFrame> frame = known(portal, point, radius)) {
        return frame->arrival();
    }
    return create(portal, point).arrival();
}

std::optional<QcPortalFrame> QcPortalLinks::known(const QcPortalDefinition &portal, const QcVec3 &point, double radius)
{
    QcPortalIndex &index = m_state.current().portals();
    const QList<QcPortalIndex::Entry> candidates = index.near(portal.name, point, radius);
    for (const QcPortalIndex::Entry &entry : candidates) {
        m_state.world().loadChunk(entry.frame.origin.chunk());
        const std::optional<QcBlockId> block = m_state.world().blockAt(entry.frame.origin);
        if (block && QcPortalShapes::portalOfBlock(m_state.content(), *block) == &portal) {
            return entry.frame;
        }
        index.remove(entry);
    }
    return std::nullopt;
}

QcPortalFrame QcPortalLinks::create(const QcPortalDefinition &portal, const QcVec3 &point)
{
    const QcPortalFrame shape{{}, Axis::X, QcPortalDefinition::innerOf(portal.minWidth), QcPortalDefinition::innerOf(portal.minHeight)};
    const int highest = highestFloor(m_state.current().definition(), shape.height);
    loadAround(point, portal.createRadius + shape.width + 2);
    // First with room to step out onto ground, then along the frame alone (one may drop when stepping
    // out, as in the role model), last at the point on a platform (decision E-1025).
    std::optional<QcPortalFrame> found = SpotSearch(m_state.world(), shape, highest, 1).nearest(point, portal.createRadius);
    if (!found) {
        found = SpotSearch(m_state.world(), shape, highest, 0).nearest(point, portal.createRadius);
    }
    QcPortalFrame frame = found.value_or(shape);
    if (!found) {
        frame.origin = {floorOf(point.x), std::clamp(floorOf(point.y), lowestFloor(m_state.current().definition(), highest), highest), floorOf(point.z)};
    }

    // Where it must stand, it gets a floor to step out on and room around it (decision E-1025).
    const QString platformName = portal.platform.isEmpty() ? portal.frame : portal.platform;
    const QcBlockId platform = m_state.content().blockId(platformName).value_or(qcAirBlockId);
    const QcBlockId frameBlock = m_state.content().blockId(portal.frame).value_or(platform);
    for (int across = -1; across <= frame.width; ++across) {
        for (int up = -1; up <= frame.height; ++up) {
            const bool inner = across >= 0 && across < frame.width && up >= 0 && up < frame.height;
            m_blocks.setAndBroadcast(frame.at(across, up), inner ? qcAirBlockId : frameBlock);
        }
    }
    for (int across = 0; !found && across < frame.width; ++across) {
        for (const int step : {-1, 1}) {
            m_blocks.setAndBroadcast(beside(frame, across, -1, step), platform);
            for (int up = 0; up < 3; ++up) {
                m_blocks.setAndBroadcast(beside(frame, across, up, step), qcAirBlockId);
            }
        }
    }
    light(portal, frame);
    return frame;
}

QcVec3 QcPortalLinks::fixed(QcServerDimension &target, const QcDimensionDefinition::Arrival &arrival)
{
    const QcServerState::Scope scope(m_state, target);
    const QcVec3 point{arrival.position.x + 0.5, static_cast<double>(arrival.position.y), arrival.position.z + 0.5};
    loadAround(point, arrival.platformRadius + 1);
    const std::optional<QcBlockId> platform = m_state.content().blockId(arrival.platform);
    for (int dx = -arrival.platformRadius; dx <= arrival.platformRadius; ++dx) {
        for (int dz = -arrival.platformRadius; dz <= arrival.platformRadius; ++dz) {
            makeRoom(arrival.position.offset(dx, -1, dz), platform);
        }
    }
    return point;
}

void QcPortalLinks::makeRoom(QcBlockPos floor, std::optional<QcBlockId> platform)
{
    if (platform && m_state.world().blockAt(floor) != platform) {
        m_blocks.setAndBroadcast(floor, *platform);
    }
    for (int up = 1; up <= 3; ++up) {
        if (m_state.world().blockAt(floor.offset(0, up, 0)) != qcAirBlockId) {
            m_blocks.setAndBroadcast(floor.offset(0, up, 0), qcAirBlockId);
        }
    }
}

bool QcPortalLinks::light(const QcPortalDefinition &portal, const QcPortalFrame &frame)
{
    const std::optional<QcBlockId> fill = QcPortalShapes::fillState(m_state.content(), portal, frame.axis);
    if (!fill) {
        return false;
    }
    const QList<QcBlockPos> cells = frame.cells();
    for (const QcBlockPos &cell : cells) {
        m_blocks.setAndBroadcast(cell, *fill);
    }
    m_state.current().portals().add({portal.name, frame});
    return true;
}

void QcPortalLinks::loadAround(const QcVec3 &point, int radius)
{
    const QcChunkPos low = QcChunkStreaming::chunkOf(point - QcVec3{static_cast<double>(radius), 0.0, static_cast<double>(radius)});
    const QcChunkPos high = QcChunkStreaming::chunkOf(point + QcVec3{static_cast<double>(radius), 0.0, static_cast<double>(radius)});
    for (int x = low.x; x <= high.x; ++x) {
        for (int z = low.z; z <= high.z; ++z) {
            m_state.world().loadChunk({x, z});
        }
    }
}
