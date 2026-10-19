#include "server/villages/QcVillagerRoads.h"

#include "worldgen/villages/QcVillagePlan.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace {

constexpr std::array<std::pair<int, int>, 8> directions{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};

quint64 columnKey(int x, int z)
{
    return (quint64(quint32(x)) << 32) | quint32(z);
}

bool withinReach(QcBlockPos a, QcBlockPos b, int reach)
{
    return std::abs(a.x - b.x) <= reach && std::abs(a.z - b.z) <= reach;
}

} // namespace

std::optional<QcBlockPos> QcVillagerRoads::nextLeg(const QcVillagePlan &plan, QcBlockPos from, QcBlockPos to)
{
    if (withinReach(from, to, legReach)) {
        return std::nullopt;
    }
    Graph &graph = graphOf(plan);
    const int goal = nearest(graph, to, false);
    const int start = nearest(graph, from, true);
    if (goal < 0 || start < 0) {
        return std::nullopt;
    }

    graph.measureFrom(goal);
    ++m_planned;
    // The road cell within reach nearest to the goal along the roads; one no closer than the
    // walker's own road cell is no help.
    int best = -1;
    int bestDistance = std::numeric_limits<int>::max();
    for (std::size_t i = 0; i < graph.cells.size(); ++i) {
        const QcBlockPos cell = graph.cells[i];
        const int distance = graph.distance[i];
        if (distance >= 0 && distance < bestDistance && withinReach(cell, from, legReach) && std::abs(cell.y - from.y) <= legHeight) {
            best = int(i);
            bestDistance = distance;
        }
    }
    const int own = graph.distance[std::size_t(start)];
    if (best < 0 || (own >= 0 && bestDistance >= own)) {
        return std::nullopt;
    }
    return graph.cells[std::size_t(best)];
}

std::optional<QcBlockPos> QcVillagerRoads::wayOutOfWater(const QcVillagePlan &plan, QcBlockPos from)
{
    const Graph &graph = graphOf(plan);
    ++m_planned;
    std::optional<QcBlockPos> best;
    qint64 bestDistance = std::numeric_limits<qint64>::max();
    for (std::size_t i = 0; i < graph.landCells; ++i) {
        const QcBlockPos cell = graph.cells[i];
        const qint64 dx = cell.x - from.x;
        const qint64 dz = cell.z - from.z;
        if (dx * dx + dz * dz < bestDistance && std::abs(cell.y - from.y) <= legHeight) {
            best = cell;
            bestDistance = dx * dx + dz * dz;
        }
    }
    return best;
}

QcVillagerRoads::Graph &QcVillagerRoads::graphOf(const QcVillagePlan &plan)
{
    m_used.removeOne(plan.id);
    m_used.append(plan.id);
    auto it = m_graphs.find(plan.id);
    if (it == m_graphs.end()) {
        it = m_graphs.insert(plan.id, build(plan));
    }
    Graph &graph = **it;
    while (m_used.size() > maxGraphs) {
        m_graphs.remove(m_used.takeFirst());
    }
    return graph;
}

std::shared_ptr<QcVillagerRoads::Graph> QcVillagerRoads::build(const QcVillagePlan &plan)
{
    auto graph = std::make_shared<Graph>();
    QHash<quint64, int> index;
    const auto add = [&](QcBlockPos ground) {
        const QcBlockPos stand = ground.offset(0, 1, 0);
        if (!index.contains(columnKey(stand.x, stand.z))) {
            index.insert(columnKey(stand.x, stand.z), int(graph->cells.size()));
            graph->cells.push_back(stand);
        }
    };
    for (const QcBlockPos &cell : plan.roads) {
        add(cell);
    }
    for (const QcBlockPos &cell : plan.branchRoads) {
        add(cell);
    }
    graph->landCells = graph->cells.size();
    for (const QcVillagePlan::Deck &deck : plan.decks) {
        add(deck.at);
    }

    graph->neighbours.resize(graph->cells.size());
    for (std::size_t i = 0; i < graph->cells.size(); ++i) {
        const QcBlockPos cell = graph->cells[i];
        for (std::size_t d = 0; d < directions.size(); ++d) {
            const int other = index.value(columnKey(cell.x + directions[d].first, cell.z + directions[d].second), -1);
            const bool joined = other >= 0 && std::abs(graph->cells[std::size_t(other)].y - cell.y) <= 1;
            graph->neighbours[i][d] = joined ? other : -1;
        }
    }
    graph->distance.assign(graph->cells.size(), -1);
    return graph;
}

int QcVillagerRoads::nearest(const Graph &graph, QcBlockPos pos, bool walker)
{
    // Cells out of a walker's step come after all within it.
    constexpr qint64 outOfStep = qint64(1) << 40;
    int best = -1;
    qint64 bestScore = std::numeric_limits<qint64>::max();
    for (std::size_t i = 0; i < graph.cells.size(); ++i) {
        const QcBlockPos cell = graph.cells[i];
        const qint64 dx = cell.x - pos.x;
        const qint64 dz = cell.z - pos.z;
        const int dy = std::abs(cell.y - pos.y);
        const qint64 score = (dx * dx + dz * dz) * 4 + dy + (walker && dy > 1 ? outOfStep : 0);
        if (score < bestScore) {
            best = int(i);
            bestScore = score;
        }
    }
    return best;
}

void QcVillagerRoads::Graph::measureFrom(int start)
{
    std::ranges::fill(distance, -1);
    queue.clear();
    distance[std::size_t(start)] = 0;
    queue.push_back(start);
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int cell = queue[head];
        for (const int next : neighbours[std::size_t(cell)]) {
            if (next >= 0 && distance[std::size_t(next)] < 0) {
                distance[std::size_t(next)] = distance[std::size_t(cell)] + 1;
                queue.push_back(next);
            }
        }
    }
}
