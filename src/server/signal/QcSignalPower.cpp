#include "server/signal/QcSignalPower.h"

#include "server/blocks/QcBlockRules.h"

#include <algorithm>

namespace {

bool isSource(QcSignalKind kind)
{
    return kind == QcSignalKind::Lever || kind == QcSignalKind::Button || kind == QcSignalKind::Plate || kind == QcSignalKind::Torch
           || kind == QcSignalKind::Repeater || kind == QcSignalKind::Comparator || kind == QcSignalKind::Observer
           || kind == QcSignalKind::Constant;
}

// Whether dust points at the horizontal side `towards`: a line or arm on that side, or a lone dot,
// which points everywhere.
bool dustPoints(QcSignalState state, QcBlockFace towards)
{
    const int index = QcSignalTable::horizontalIndex(towards);
    if (index < 0) {
        return false;
    }
    const bool lone = std::ranges::all_of(state.sides, [](QcDustSide side) { return side == QcDustSide::None; });
    return lone || state.sides[std::size_t(index)] != QcDustSide::None;
}

} // namespace

QcSignalPower::QcSignalPower(const QcBlockAccess &world, const QcSignalTable &table)
    : m_world(world)
    , m_table(table)
{
}

const QcSignalState &QcSignalPower::at(QcBlockPos pos) const
{
    const std::optional<QcBlockId> block = m_world.blockAt(pos);
    return m_table.at(block.value_or(qcAirBlockId));
}

int QcSignalPower::directPower(QcSignalState state, QcBlockFace towards)
{
    switch (state.kind) {
    case QcSignalKind::Lever:
    case QcSignalKind::Button:
    case QcSignalKind::Plate:
    case QcSignalKind::Constant:
        return state.power;
    case QcSignalKind::Torch:
        return state.on && state.facing != towards ? state.power : 0;
    case QcSignalKind::Repeater:
    case QcSignalKind::Comparator:
        return state.facing == towards ? state.power : 0;
    case QcSignalKind::Observer:
        return state.facing && QcBlockRules::opposite(*state.facing) == towards ? state.power : 0;
    case QcSignalKind::Dust:
        if (towards == QcBlockFace::Down) {
            return state.power;
        }
        return dustPoints(state, towards) ? state.power : 0;
    default:
        return 0;
    }
}

int QcSignalPower::strongPower(QcSignalState state, QcBlockFace towards)
{
    switch (state.kind) {
    case QcSignalKind::Lever:
    case QcSignalKind::Button:
    case QcSignalKind::Repeater:
    case QcSignalKind::Comparator:
        return state.facing == towards ? state.power : 0;
    case QcSignalKind::Observer:
        return state.facing && QcBlockRules::opposite(*state.facing) == towards ? state.power : 0;
    case QcSignalKind::Plate:
        return towards == QcBlockFace::Down ? state.power : 0;
    case QcSignalKind::Torch:
        return state.on && state.strongPowersAbove && towards == QcBlockFace::Up ? state.power : 0;
    default:
        return 0;
    }
}

int QcSignalPower::strongLevel(QcBlockPos pos) const
{
    if (!at(pos).conductor) {
        return 0;
    }

    int level = 0;
    for (const QcBlockFace face : qcAllBlockFaces) {
        level = std::max(level, strongPower(at(qcNeighbour(pos, face)), QcBlockRules::opposite(face)));
    }
    return level;
}

int QcSignalPower::weakLevel(QcBlockPos pos) const
{
    if (!at(pos).conductor) {
        return 0;
    }

    int level = 0;
    for (const QcBlockFace face : qcAllBlockFaces) {
        const QcSignalState &neighbour = at(qcNeighbour(pos, face));
        const QcBlockFace back = QcBlockRules::opposite(face);
        level = std::max({level, strongPower(neighbour, back), neighbour.kind == QcSignalKind::Dust ? directPower(neighbour, back) : 0});
    }
    return level;
}

int QcSignalPower::inputFrom(QcBlockPos pos, QcBlockFace face) const
{
    const QcBlockPos neighbourPos = qcNeighbour(pos, face);
    const QcSignalState &neighbour = at(neighbourPos);
    if (isSource(neighbour.kind) || neighbour.kind == QcSignalKind::Dust) {
        return directPower(neighbour, QcBlockRules::opposite(face));
    }
    return neighbour.conductor ? weakLevel(neighbourPos) : 0;
}

int QcSignalPower::input(QcBlockPos pos, quint8 ignoredFaces) const
{
    int level = 0;
    for (const QcBlockFace face : qcAllBlockFaces) {
        if ((ignoredFaces & (1U << unsigned(face))) == 0) {
            level = std::max(level, inputFrom(pos, face));
        }
    }
    return level;
}

int QcSignalPower::dustInput(QcBlockPos pos) const
{
    int level = 0;
    for (const QcBlockFace face : qcAllBlockFaces) {
        const QcBlockPos neighbourPos = qcNeighbour(pos, face);
        const QcSignalState &neighbour = at(neighbourPos);
        if (isSource(neighbour.kind)) {
            level = std::max(level, directPower(neighbour, QcBlockRules::opposite(face)));
        } else if (neighbour.conductor) {
            level = std::max(level, strongLevel(neighbourPos));
        }
    }
    return level;
}

std::optional<QcChunkPos> QcSignalPower::unloadedNear(QcBlockPos pos) const
{
    for (const int dx : {-2, 2}) {
        for (const int dz : {-2, 2}) {
            const QcBlockPos corner{pos.x + dx, std::clamp(pos.y, 0, QcWorld::worldHeight - 1), pos.z + dz};
            if (!m_world.blockAt(corner)) {
                return corner.chunk();
            }
        }
    }
    return std::nullopt;
}

QcSignalPower::Positions QcSignalPower::targets(QcBlockPos pos, QcSignalState state)
{
    Positions cells;
    switch (state.kind) {
    case QcSignalKind::Lever:
    case QcSignalKind::Button:
    case QcSignalKind::Repeater:
    case QcSignalKind::Comparator:
        if (state.facing) {
            cells.append(qcNeighbour(pos, *state.facing));
        }
        break;
    case QcSignalKind::Observer:
        if (state.facing) {
            cells.append(qcNeighbour(pos, QcBlockRules::opposite(*state.facing)));
        }
        break;
    case QcSignalKind::Plate:
        cells.append(qcNeighbour(pos, QcBlockFace::Down));
        break;
    case QcSignalKind::Torch:
        if (state.strongPowersAbove) {
            cells.append(qcNeighbour(pos, QcBlockFace::Up));
        }
        break;
    case QcSignalKind::Dust:
        cells.append(qcNeighbour(pos, QcBlockFace::Down));
        for (const QcBlockFace face : QcSignalTable::horizontal) {
            if (dustPoints(state, face)) {
                cells.append(qcNeighbour(pos, face));
            }
        }
        break;
    default:
        break;
    }
    return cells;
}
