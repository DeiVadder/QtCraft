#include "QcDustNetwork.h"

#include "QcBlockRules.h"

#include <algorithm>

namespace {

// Sources, plates and torches take dust from every side, repeaters only along their axis.
bool connectsSideways(QcSignalState state, QcBlockFace towards)
{
    switch (state.kind) {
    case QcSignalKind::Lever:
    case QcSignalKind::Button:
    case QcSignalKind::Plate:
    case QcSignalKind::Torch:
        return true;
    case QcSignalKind::Repeater:
        return state.facing == towards || state.facing == QcBlockRules::opposite(towards);
    default:
        return false;
    }
}

} // namespace

struct QcDustNetwork::Region
{
    QList<QcBlockPos> nodes;
    // Indices into `nodes` of the dust each node links to inside the region.
    QList<QList<int>> links;
    // What the dust linked to each node from outside the region gives it (its strength minus one).
    QList<int> outside;
    std::optional<QcChunkPos> blockedBy;
};

QcDustNetwork::QcDustNetwork(const QcSignalPower &power)
    : m_power(power)
{
}

std::array<QcDustSide, 4> QcDustNetwork::sidesOf(QcBlockPos pos) const
{
    const bool coveredAbove = m_power.at(pos.offset(0, 1, 0)).conductor;
    std::array<QcDustSide, 4> sides{};
    int connected = 0;

    for (std::size_t i = 0; i < QcSignalTable::horizontal.size(); ++i) {
        const QcBlockFace face = QcSignalTable::horizontal[i];
        const QcBlockPos beside = qcNeighbour(pos, face);
        const QcSignalState &neighbour = m_power.at(beside);
        const bool flat = neighbour.kind == QcSignalKind::Dust || connectsSideways(neighbour, QcBlockRules::opposite(face));
        const bool up = !flat && !coveredAbove && isDust(beside.offset(0, 1, 0));
        const bool down = !flat && !up && !neighbour.conductor && isDust(beside.offset(0, -1, 0));
        if (up) {
            sides[i] = QcDustSide::Up;
        } else if (flat || down) {
            sides[i] = QcDustSide::Side;
        }
        connected += sides[i] != QcDustSide::None ? 1 : 0;
    }

    // A single connection runs on as a straight line through the dust.
    if (connected == 1) {
        for (std::size_t i = 0; i < sides.size(); ++i) {
            if (sides[i] != QcDustSide::None && sides[(i + 2) % sides.size()] == QcDustSide::None) {
                sides[(i + 2) % sides.size()] = QcDustSide::Side;
                break;
            }
        }
    }
    return sides;
}

QList<QcBlockPos> QcDustNetwork::links(QcBlockPos pos) const
{
    const bool coveredAbove = m_power.at(pos.offset(0, 1, 0)).conductor;
    QList<QcBlockPos> linked;
    for (const QcBlockFace face : QcSignalTable::horizontal) {
        const QcBlockPos beside = qcNeighbour(pos, face);
        if (isDust(beside)) {
            linked.append(beside);
        } else if (!coveredAbove && isDust(beside.offset(0, 1, 0))) {
            linked.append(beside.offset(0, 1, 0));
        } else if (!m_power.at(beside).conductor && isDust(beside.offset(0, -1, 0))) {
            linked.append(beside.offset(0, -1, 0));
        }
    }
    return linked;
}

QcDustNetwork::Region QcDustNetwork::collect(const std::vector<QcBlockPos> &seeds) const
{
    Region region;
    QHash<QcBlockPos, int> index;
    QList<int> depth;
    for (const QcBlockPos &seed : seeds) {
        if (!index.contains(seed) && isDust(seed) && region.nodes.size() < maxNodes) {
            index.insert(seed, int(region.nodes.size()));
            region.nodes.append(seed);
            depth.append(0);
        }
    }

    // Breadth first up to the reach of a signal: farther dust cannot change.
    QList<QList<QcBlockPos>> linkedPositions;
    for (qsizetype next = 0; next < region.nodes.size(); ++next) {
        if (!region.blockedBy) {
            region.blockedBy = m_power.unloadedNear(region.nodes[next]);
        }
        const QList<QcBlockPos> linked = links(region.nodes[next]);
        for (const QcBlockPos &pos : linked) {
            if (depth[next] < QcSignalDefinition::maxPower && !index.contains(pos) && region.nodes.size() < maxNodes) {
                index.insert(pos, int(region.nodes.size()));
                region.nodes.append(pos);
                depth.append(depth[next] + 1);
            }
        }
        linkedPositions.append(linked);
    }

    region.links.resize(region.nodes.size());
    region.outside.fill(0, region.nodes.size());
    for (qsizetype i = 0; i < region.nodes.size(); ++i) {
        for (const QcBlockPos &pos : std::as_const(linkedPositions[i])) {
            if (const auto it = index.constFind(pos); it != index.constEnd()) {
                region.links[i].append(*it);
            } else {
                region.outside[i] = std::max(region.outside[i], m_power.at(pos).power - 1);
            }
        }
    }
    return region;
}

void QcDustNetwork::spread(const QList<int> &linked, int level, QList<int> &inputs, QList<int> &bucket)
{
    for (const int node : linked) {
        if (inputs[node] < level) {
            inputs[node] = level;
            bucket.append(node);
        }
    }
}

QList<int> QcDustNetwork::strengths(const Region &region, QList<int> inputs)
{
    // Buckets per strength: each dust is settled at the strongest level that reaches it.
    std::array<QList<int>, QcSignalDefinition::maxPower + 1> buckets;
    for (qsizetype i = 0; i < inputs.size(); ++i) {
        buckets[std::size_t(inputs[i])].append(int(i));
    }

    // A bucket is read while only the next weaker one grows.
    for (int level = QcSignalDefinition::maxPower; level > 1; --level) {
        for (const int node : std::as_const(buckets[std::size_t(level)])) {
            if (inputs[node] == level) {
                spread(region.links[node], level - 1, inputs, buckets[std::size_t(level - 1)]);
            }
        }
    }
    return inputs;
}

void QcDustNetwork::recompute(const std::vector<QcBlockPos> &seeds, Result &result) const
{
    const Region region = collect(seeds);
    result.visited += region.nodes.size();
    if (region.nodes.isEmpty()) {
        return;
    }
    if (region.blockedBy) {
        result.blockedBy = region.blockedBy;
        return;
    }

    QList<int> inputs;
    inputs.reserve(region.nodes.size());
    for (qsizetype i = 0; i < region.nodes.size(); ++i) {
        inputs.append(std::max(m_power.dustInput(region.nodes[i]), region.outside[i]));
    }

    const QList<int> levels = strengths(region, std::move(inputs));
    for (qsizetype i = 0; i < region.nodes.size(); ++i) {
        const QcBlockPos pos = region.nodes[i];
        const std::optional<QcBlockId> current = m_power.blockAt(pos);
        if (!current) {
            continue;
        }
        const QcBlockId wanted = m_power.table().dustState(*current, levels[i], sidesOf(pos));
        if (wanted != *current) {
            result.changes.append({pos, wanted});
        }
    }
}
