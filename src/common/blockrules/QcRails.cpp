// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/blockrules/QcRails.h"

#include "common/blockrules/QcBlockRules.h"
#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcRails {

namespace {

constexpr std::array<QcBlockFace, 4> sides = {QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West};

// How a side of a rail meets the rail beside it.
enum class Level : quint8 { None, Level, Higher, Lower };

[[nodiscard]] std::optional<QcBlockFace> sideFromName(const QString &name)
{
    for (const QcBlockFace side : sides) {
        if (QcBlockRules::faceName(side) == name) {
            return side;
        }
    }
    return std::nullopt;
}

std::size_t indexOf(QcBlockFace side)
{
    return static_cast<std::size_t>(std::ranges::find(sides, side) - sides.begin());
}

// The side clockwise of `side` seen from above.
QcBlockFace clockwise(QcBlockFace side)
{
    return sides[(indexOf(side) + 1) % sides.size()];
}

bool isNorthOrEast(QcBlockFace side)
{
    return side == QcBlockFace::North || side == QcBlockFace::East;
}

// The canonical facing of a straight track along the axis of `side` (north or east).
QcBlockFace axisOf(QcBlockFace side)
{
    return isNorthOrEast(side) ? side : QcBlockRules::opposite(side);
}

QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::Straight:
        return u"straight"_s;
    case Kind::Ascending:
        return u"ascending"_s;
    case Kind::Curve:
        return u"curve"_s;
    }
    return {};
}

// Whether the rail can bend (its shape state has `curve`; powered, detector and activator rails cannot).
bool canCurve(const QcContentRegistry &content, QcBlockId id)
{
    const QcBlockProperty *shape = content.block(id).property(u"shape"_s);
    return shape && shape->values.contains(u"curve"_s);
}

// Whether any side's neighbour column lies in a chunk that is not loaded.
bool besideUnloaded(const QcBlockAccess &world, QcBlockPos pos)
{
    return std::ranges::any_of(sides, [&](QcBlockFace side) { return !world.blockAt(qcNeighbour(pos, side)); });
}

// How the side `side` of the rail at `pos` meets a rail beside it, by where rails lie alone.
Level levelTowards(const QcBlockAccess &world, QcBlockPos pos, QcBlockFace side)
{
    const QcContentRegistry &content = world.content();
    const QcBlockPos beside = qcNeighbour(pos, side);
    const auto railAt = [&](QcBlockPos at) {
        const std::optional<QcBlockId> block = world.blockAt(at);
        return block && isRail(content, *block);
    };
    if (railAt(beside)) {
        return Level::Level;
    }
    if (railAt(beside.offset(0, 1, 0))) {
        return Level::Higher;
    }
    return railAt(beside.offset(0, -1, 0)) ? Level::Lower : Level::None;
}

std::array<Level, 4> levelsAround(const QcBlockAccess &world, QcBlockPos pos)
{
    std::array<Level, 4> levels{};
    for (const QcBlockFace side : sides) {
        levels[indexOf(side)] = levelTowards(world, pos, side);
    }
    return levels;
}

// The axis a rail keeps when its neighbours do not decide (as the facing of a straight track).
QcBlockFace keptAxis(const QcContentRegistry &content, QcBlockId id)
{
    const std::optional<QcBlockFace> facing = sideFromName(content.property(id, u"facing"_s));
    return axisOf(facing.value_or(QcBlockFace::North));
}

// A straight track along the axis of `side`, rising towards the one end with a rail above.
QcBlockId straightState(const QcContentRegistry &content, QcBlockId id, QcBlockFace side, std::array<Level, 4> levels)
{
    const QcBlockFace back = QcBlockRules::opposite(side);
    const bool upFront = levels[indexOf(side)] == Level::Higher;
    const bool upBack = levels[indexOf(back)] == Level::Higher;
    if (upFront != upBack) {
        return stateOf(content, id, Kind::Ascending, upFront ? side : back);
    }
    return stateOf(content, id, Kind::Straight, axisOf(side));
}

// The state of the rail `id` with rails on the sides `levels`. Two sides at a right angle bend;
// otherwise the track runs straight along the pair of opposite sides with rails (three sides: the
// pair, ignoring the branch), along the only side with a rail, or where the neighbours do not decide
// (none or all four, or a corner for a rail that cannot bend) along the player's view or its axis.
QcBlockId shapeFrom(const QcContentRegistry &content, QcBlockId id, std::array<Level, 4> levels, std::optional<QcBlockFace> lookSide)
{
    QList<QcBlockFace> connected;
    for (const QcBlockFace side : sides) {
        if (levels[indexOf(side)] != Level::None) {
            connected.append(side);
        }
    }

    const bool corner = connected.size() == 2 && connected[1] != QcBlockRules::opposite(connected[0]);
    if (corner && canCurve(content, id)) {
        const QcBlockFace first = clockwise(connected[0]) == connected[1] ? connected[0] : connected[1];
        return stateOf(content, id, Kind::Curve, first);
    }
    if (connected.size() == 3) {
        const auto paired = std::ranges::find_if(connected, [&](QcBlockFace side) { return connected.contains(QcBlockRules::opposite(side)); });
        return straightState(content, id, *paired, levels);
    }
    if (connected.size() == 1 || (connected.size() == 2 && !corner)) {
        return straightState(content, id, connected[0], levels);
    }
    return straightState(content, id, lookSide ? axisOf(*lookSide) : keptAxis(content, id), levels);
}

// Whether the rail one block higher (`higher`) or lower beside side `side` of the rail at `pos`,
// shaped by the rails around it alone, has an end where this rail's end on that side would lie;
// nothing while a neighbour of it lies in a chunk that is not loaded.
std::optional<bool> meets(const QcBlockAccess &world, QcBlockPos pos, QcBlockFace side, bool higher)
{
    const QcContentRegistry &content = world.content();
    const QcBlockPos other = qcNeighbour(pos, side).offset(0, higher ? 1 : -1, 0);
    if (besideUnloaded(world, other)) {
        return std::nullopt;
    }
    const QcBlockId block = world.blockAt(other).value_or(qcAirBlockId);
    const std::optional<Track> track = trackOf(content, shapeFrom(content, block, levelsAround(world, other), std::nullopt));
    const QcVec3 point = endPoint(pos, End{side, higher});
    return track && std::ranges::any_of(track->ends, [&](End end) { return endPoint(other, end) == point; });
}

// The offsets of shapeNeighbours(): the slope neighbours, and two steps sideways (twice the same
// side, two sides at a right angle or back to the start) at most two blocks up or down, without the
// cell itself and the cells above and below it.
constexpr std::array<std::array<int, 3>, shapeNeighbourCount> shapeOffsets = [] {
    constexpr std::array<std::array<int, 2>, 4> steps{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
    std::array<std::array<int, 3>, shapeNeighbourCount> offsets{};
    std::size_t count = 0;
    const auto add = [&](int x, int y, int z) {
        for (std::size_t i = 0; i < count; ++i) {
            if (offsets[i] == std::array<int, 3>{x, y, z}) {
                return;
            }
        }
        offsets[count++] = {x, y, z};
    };
    for (const auto &step : steps) {
        add(step[0], 1, step[1]);
        add(step[0], -1, step[1]);
    }
    for (const auto &first : steps) {
        for (const auto &second : steps) {
            for (int y = -2; y <= 2; ++y) {
                const int x = first[0] + second[0];
                const int z = first[1] + second[1];
                if (x != 0 || z != 0 || (y != 0 && y != 1 && y != -1)) {
                    add(x, y, z);
                }
            }
        }
    }
    return offsets;
}();
static_assert(std::ranges::none_of(shapeOffsets, [](std::array<int, 3> offset) { return offset == std::array<int, 3>{0, 0, 0}; }),
              "every offset is set, none is the cell itself");

} // namespace

bool isRail(const QcContentRegistry &content, QcBlockId block)
{
    return content.isValidBlock(block) && content.block(block).rail;
}

std::optional<Track> trackOf(const QcContentRegistry &content, QcBlockId block)
{
    if (!isRail(content, block)) {
        return std::nullopt;
    }

    const QcBlockFace facing = sideFromName(content.property(block, u"facing"_s)).value_or(QcBlockFace::North);
    const QString shape = content.property(block, u"shape"_s);
    if (shape == u"ascending"_s) {
        return Track{{End{facing, true}, End{QcBlockRules::opposite(facing), false}}};
    }
    if (shape == u"curve"_s) {
        return Track{{End{facing, false}, End{clockwise(facing), false}}};
    }
    return Track{{End{facing, false}, End{QcBlockRules::opposite(facing), false}}};
}

QcVec3 endPoint(QcBlockPos cell, End end)
{
    const QcFaceOffset offset = qcFaceOffset(end.side);
    return {cell.x + 0.5 + 0.5 * offset.dx, static_cast<double>(cell.y) + (end.raised ? 1.0 : 0.0), cell.z + 0.5 + 0.5 * offset.dz};
}

QcBlockId stateOf(const QcContentRegistry &content, QcBlockId block, Kind kind, QcBlockFace facing)
{
    const QcBlockId shaped = content.withProperty(block, u"shape"_s, kindName(kind));
    return content.withProperty(shaped, u"facing"_s, QcBlockRules::faceName(facing));
}

QcBlockId shapedState(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id, std::optional<QcBlockFace> lookSide)
{
    const QcContentRegistry &content = world.content();
    if (!isRail(content, id) || besideUnloaded(world, pos)) {
        return id;
    }

    // A rail one higher or lower counts only if it reaches this rail (decision E-382): a line of
    // rails passing beside and above does not bend or raise the rail at the end of another line.
    std::array<Level, 4> levels = levelsAround(world, pos);
    for (const QcBlockFace side : sides) {
        Level &level = levels[indexOf(side)];
        if (level != Level::Higher && level != Level::Lower) {
            continue;
        }
        const std::optional<bool> met = meets(world, pos, side, level == Level::Higher);
        if (!met) {
            return id;
        }
        if (!*met) {
            level = Level::None;
        }
    }
    return shapeFrom(content, id, levels, lookSide);
}

std::array<QcBlockPos, 8> slopeNeighbours(QcBlockPos pos)
{
    std::array<QcBlockPos, 8> positions;
    std::size_t next = 0;
    for (const QcBlockFace side : sides) {
        const QcBlockPos beside = qcNeighbour(pos, side);
        positions[next++] = beside.offset(0, 1, 0);
        positions[next++] = beside.offset(0, -1, 0);
    }
    return positions;
}

std::array<QcBlockPos, shapeNeighbourCount> shapeNeighbours(QcBlockPos pos)
{
    std::array<QcBlockPos, shapeNeighbourCount> positions;
    for (std::size_t i = 0; i < shapeOffsets.size(); ++i) {
        positions[i] = pos.offset(shapeOffsets[i][0], shapeOffsets[i][1], shapeOffsets[i][2]);
    }
    return positions;
}

std::optional<Joint> joined(const QcBlockAccess &world, const Table &rails, QcBlockPos cell, Track track, int end)
{
    const QcVec3 point = endPoint(cell, track.ends[static_cast<std::size_t>(end)]);
    const QcBlockPos beside = qcNeighbour(cell, track.ends[static_cast<std::size_t>(end)].side);
    for (const QcBlockPos next : {beside, beside.offset(0, 1, 0), beside.offset(0, -1, 0)}) {
        const std::optional<QcBlockId> block = world.blockAt(next);
        const std::optional<Track> &other = block ? rails.trackOf(*block) : std::nullopt;
        if (!other) {
            continue;
        }
        for (int entry = 0; entry < 2; ++entry) {
            if (endPoint(next, other->ends[static_cast<std::size_t>(entry)]) == point) {
                return Joint{next, *other, entry};
            }
        }
    }
    return std::nullopt;
}

Line lineOf(const QcBlockAccess &world, const Table &rails, QcBlockPos cell, int reach)
{
    Line line;
    const std::optional<QcBlockId> start = world.blockAt(cell);
    const std::optional<Track> &track = start ? rails.trackOf(*start) : std::nullopt;
    if (!track) {
        return line;
    }

    const QcContentRegistry &content = world.content();
    const quint32 type = content.state(*start).type;
    std::array<QList<QcBlockPos>, 2> halves;
    for (int end = 0; end < 2; ++end) {
        QcBlockPos at = cell;
        Track current = *track;
        int out = end;
        for (int step = 0; step < reach; ++step) {
            const std::optional<Joint> next = joined(world, rails, at, current, out);
            // A loop of rails ends where it meets itself.
            if (!next || next->cell == cell || halves[0].contains(next->cell)
                || content.state(world.blockAt(next->cell).value_or(qcAirBlockId)).type != type) {
                break;
            }
            halves[static_cast<std::size_t>(end)].append(next->cell);
            at = next->cell;
            current = next->track;
            out = 1 - next->entry;
        }
    }

    std::ranges::reverse(halves[0]);
    line.cells = halves[0];
    line.index = line.cells.size();
    line.cells.append(cell);
    line.cells.append(halves[1]);
    return line;
}

Table::Table(const QcContentRegistry &content)
    : m_tracks(static_cast<std::size_t>(content.stateCount()))
    , m_behaviours(static_cast<std::size_t>(content.stateCount()))
{
    for (qsizetype id = 0; id < content.stateCount(); ++id) {
        const QcBlockDefinition &block = content.block(static_cast<QcBlockId>(id));
        if (block.rail) {
            m_tracks[static_cast<std::size_t>(id)] = QcRails::trackOf(content, static_cast<QcBlockId>(id));
            m_behaviours[static_cast<std::size_t>(id)] = {block.railAction, content.property(static_cast<QcBlockId>(id), u"powered"_s) == u"true"_s};
        }
    }
}

} // namespace QcRails
