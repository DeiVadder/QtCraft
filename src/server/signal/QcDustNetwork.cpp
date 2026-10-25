#include "server/signal/QcDustNetwork.h"

#include "common/blockrules/QcBlockRules.h"

#include <algorithm>
#include <array>
#include <utility>

namespace {

// Sources, plates, torches and comparators (side inputs) take dust from every side, repeaters only
// along their axis, observers only at their back, where their pulse leaves.
bool connectsSideways(QcSignalState state, QcBlockFace towards)
{
    switch (state.kind) {
    case QcSignalKind::Lever:
    case QcSignalKind::Button:
    case QcSignalKind::Plate:
    case QcSignalKind::Torch:
    case QcSignalKind::Comparator:
    case QcSignalKind::Constant:
    case QcSignalKind::Target:
    case QcSignalKind::Hook:
    case QcSignalKind::Viewers:
        return true;
    case QcSignalKind::Repeater:
        return state.facing == towards || state.facing == QcBlockRules::opposite(towards);
    case QcSignalKind::Observer:
        return state.facing && QcBlockRules::opposite(*state.facing) == towards;
    default:
        return false;
    }
}

// The strength of every dust of a region, settled bucket by bucket from the strongest: each dust at
// the strongest level that reaches it.
class Levels
{
public:
    explicit Levels(QList<int> inputs)
        : m_levels(std::move(inputs))
    {
        for (qsizetype i = 0; i < m_levels.size(); ++i) {
            m_buckets[static_cast<std::size_t>(m_levels[i])].append(static_cast<int>(i));
        }
    }

    [[nodiscard]] int of(int node) const { return m_levels[node]; }
    // The dust put into the bucket of `level` so far.
    [[nodiscard]] const QList<int> &bucket(int level) const { return m_buckets[static_cast<std::size_t>(level)]; }

    // Gives `level` to the linked dust that has less and puts it into that level's bucket.
    void spread(const QList<int> &linked, int level)
    {
        for (const int node : linked) {
            if (m_levels[node] < level) {
                m_levels[node] = level;
                m_buckets[static_cast<std::size_t>(level)].append(node);
            }
        }
    }

    [[nodiscard]] QList<int> take() { return std::exchange(m_levels, {}); }

private:
    QList<int> m_levels;
    std::array<QList<int>, QcSignalDefinition::maxPower + 1> m_buckets;
};

} // namespace

struct QcDustNetwork::Region
{
    QList<QcBlockPos> nodes;
    // Indices into `nodes` of the dust each node links to inside the region.
    QList<QList<int>> links;
    // The part of `links` each node gives its strength to (QcDustNetwork::feeds).
    QList<QList<int>> feeds;
    // What the dust linked to each node from outside the region gives it (its strength minus one).
    QList<int> outside;
    // Per node: an unloaded chunk it reads, if any.
    QList<std::optional<QcChunkPos>> unloaded;
    // The number of seeds: the first nodes.
    qsizetype seeds = 0;

    // Fills `links`, `feeds` and `outside` from the dust `linked` to each node (`index`: the node
    // of each position in the region). `network` is a small view of the world, passed by value.
    void join(QcDustNetwork network, const QHash<QcBlockPos, int> &index, const QList<QList<QcBlockPos>> &linked);
    // The same for the one node `node`.
    void joinNode(QcDustNetwork network, const QHash<QcBlockPos, int> &index, qsizetype node, const QList<QcBlockPos> &linked);
};

void QcDustNetwork::Region::join(QcDustNetwork network, const QHash<QcBlockPos, int> &index,
                                 const QList<QList<QcBlockPos>> &linked)
{
    links.resize(nodes.size());
    feeds.resize(nodes.size());
    outside.fill(0, nodes.size());
    for (qsizetype i = 0; i < nodes.size(); ++i) {
        joinNode(network, index, i, linked[i]);
    }
}

void QcDustNetwork::Region::joinNode(QcDustNetwork network, const QHash<QcBlockPos, int> &index, qsizetype node,
                                     const QList<QcBlockPos> &linked)
{
    const QcBlockPos pos = nodes[node];
    for (const QcBlockPos &other : linked) {
        const auto it = index.constFind(other);
        if (it == index.constEnd()) {
            // Dust just outside the region feeds it like a source, where it may feed this way.
            if (network.feeds(other, pos)) {
                outside[node] = std::max(outside[node], network.m_power.at(other).power - 1);
            }
            continue;
        }

        links[node].append(*it);
        if (network.feeds(pos, other)) {
            feeds[node].append(*it);
        }
    }
}

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

bool QcDustNetwork::feeds(QcBlockPos from, QcBlockPos to) const
{
    return to.y >= from.y || m_power.at(from.offset(0, -1, 0)).conductor;
}

QcDustNetwork::Region QcDustNetwork::collect(const std::vector<QcBlockPos> &seeds) const
{
    Region region;
    QHash<QcBlockPos, int> index;
    QList<int> depth;
    for (const QcBlockPos &seed : seeds) {
        if (!index.contains(seed) && isDust(seed) && region.nodes.size() < maxNodes) {
            index.insert(seed, static_cast<int>(region.nodes.size()));
            region.nodes.append(seed);
            depth.append(0);
        }
    }
    region.seeds = region.nodes.size();

    // Breadth first up to the reach of a signal: farther dust cannot change.
    QList<QList<QcBlockPos>> linkedPositions;
    for (qsizetype next = 0; next < region.nodes.size(); ++next) {
        region.unloaded.append(m_power.unloadedNear(region.nodes[next]));
        const QList<QcBlockPos> linked = links(region.nodes[next]);
        for (const QcBlockPos &pos : linked) {
            if (depth[next] < QcSignalDefinition::maxPower && !index.contains(pos) && region.nodes.size() < maxNodes) {
                index.insert(pos, static_cast<int>(region.nodes.size()));
                region.nodes.append(pos);
                depth.append(depth[next] + 1);
            }
        }
        linkedPositions.append(linked);
    }

    region.join(*this, index, linkedPositions);
    return region;
}

QcDustNetwork::Parts QcDustNetwork::parts(const Region &region)
{
    Parts found{QList<int>(region.nodes.size(), -1)};
    for (qsizetype start = 0; start < region.nodes.size(); ++start) {
        if (found.of[start] < 0) {
            found.add(region, start);
        }
    }
    return found;
}

void QcDustNetwork::Parts::add(const Region &region, qsizetype start)
{
    const int number = count++;
    QList<qsizetype> queue{start};
    of[start] = number;
    for (qsizetype next = 0; next < queue.size(); ++next) {
        for (const int linked : region.links[queue[next]]) {
            if (of[linked] < 0) {
                of[linked] = number;
                queue.append(linked);
            }
        }
    }
}

QcDustNetwork::Blocking QcDustNetwork::blockedParts(const Region &region, const Parts &parts)
{
    const QList<int> &part = parts.of;
    const int count = parts.count;
    Blocking blocking{QList<std::optional<QcChunkPos>>(count), {}};
    for (qsizetype i = 0; i < region.nodes.size(); ++i) {
        if (region.unloaded[i] && !blocking.ofPart[part[i]]) {
            blocking.ofPart[part[i]] = region.unloaded[i];
        }
    }

    for (int p = 0; p < count; ++p) {
        const std::optional<QcChunkPos> chunk = blocking.ofPart[p];
        if (!chunk) {
            continue;
        }
        Result::Blocked waiting{*chunk, {}};
        for (qsizetype i = 0; i < region.seeds; ++i) {
            if (part[i] == p) {
                waiting.seeds.append(region.nodes[i]);
            }
        }
        blocking.waiting.append(waiting);
    }
    return blocking;
}

QList<int> QcDustNetwork::strengths(const Region &region, QList<int> inputs)
{
    Levels levels(std::move(inputs));
    // A bucket is read while only the next weaker one grows.
    for (int level = QcSignalDefinition::maxPower; level > 1; --level) {
        for (const int node : levels.bucket(level)) {
            if (levels.of(node) == level) {
                levels.spread(region.feeds[node], level - 1);
            }
        }
    }
    return levels.take();
}

QcDustNetwork::Result QcDustNetwork::recompute(const std::vector<QcBlockPos> &seeds) const
{
    const Region region = collect(seeds);
    Result result;
    result.visited = region.nodes.size();
    if (region.nodes.isEmpty()) {
        return result;
    }

    // A part that reads a chunk that is not loaded waits as a whole; the others go on.
    const Parts linked = parts(region);
    const QList<int> &part = linked.of;
    Blocking blocking = blockedParts(region, linked);
    const QList<std::optional<QcChunkPos>> &partBlocked = blocking.ofPart;
    result.blocked = std::move(blocking.waiting);

    QList<int> inputs;
    inputs.reserve(region.nodes.size());
    for (qsizetype i = 0; i < region.nodes.size(); ++i) {
        inputs.append(std::max(m_power.dustInput(region.nodes[i]), region.outside[i]));
    }

    const QList<int> levels = strengths(region, std::move(inputs));
    for (qsizetype i = 0; i < region.nodes.size(); ++i) {
        const QcBlockPos pos = region.nodes[i];
        const std::optional<QcBlockId> current = m_power.blockAt(pos);
        if (!current || partBlocked[part[i]]) {
            continue;
        }
        const QcBlockId wanted = m_power.table().dustState(*current, levels[i], sidesOf(pos));
        if (wanted != *current) {
            result.changes.append({pos, wanted});
        }
    }
    return result;
}
