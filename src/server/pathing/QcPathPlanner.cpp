#include "server/pathing/QcPathPlanner.h"

#include "common/physics/QcPhysics.h"
#include "server/entities/QcCreature.h"

#include <algorithm>
#include <limits>

namespace {

// Back-off after a path that does not get there: one second, doubling up to eight.
constexpr int firstWaitTicks = 20;
constexpr int longestWaitTicks = 160;

} // namespace

QcPathPlanner::QcPathPlanner(const QcContentRegistry &content)
    : m_cells(content)
{
}

void QcPathPlanner::setLimits(qint64 nodesPerTick, qint64 nodesPerSearch, qint64 pathLength)
{
    m_nodesPerTick = int(nodesPerTick);
    m_nodesPerSearch = int(nodesPerSearch);
    m_pathLength = int(pathLength);
}

void QcPathPlanner::request(QcCreature &mob)
{
    if (!mob.path.queued) {
        mob.path.queued = true;
        m_queue.push_back(mob.id);
    }
}

int QcPathPlanner::nodesFor(const QcCreature &mob) const
{
    const int search = std::min(m_nodesPerSearch, m_nodesPerTick);
    return mob.path.purpose == QcMobPath::Purpose::Wander ? std::max(16, search / 4) : search;
}

void QcPathPlanner::serve(const QcBlockAccess &world, QList<QcCreature> &mobs, const std::function<QcPathRules(const QcCreature &)> &rulesOf)
{
    m_nodesLastTick = 0;
    m_searchesLastTick = 0;
    if (m_queue.empty()) {
        return;
    }

    findQueued(mobs);

    // Path finding switched off: the waiting creatures walk straight (QcMobNavigator).
    if (!enabled()) {
        for (const quint32 id : m_queue) {
            if (const qsizetype index = queuedIndex(id); index >= 0) {
                mobs[index].path.queued = false;
            }
        }
        m_queue.clear();
        return;
    }

    int budget = m_nodesPerTick;
    while (!m_queue.empty()) {
        const qsizetype index = queuedIndex(m_queue.front());
        // Gone, resting in an unloaded chunk or without a goal: the request lapses.
        if (index < 0 || mobs[index].dormant || !mobs[index].path.hasGoal()) {
            if (index >= 0) {
                mobs[index].path.queued = false;
            }
            m_queue.pop_front();
            continue;
        }

        QcCreature &mob = mobs[index];
        const int nodes = nodesFor(mob);
        if (nodes > budget) {
            break;
        }

        const QcBlockPos goal = QcPhysics::blockContaining(mob.path.goal);
        const QcPathRequest request{QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, 0.1, 0.0}), goal,
                                    mob.path.closeEnough, nodes, m_pathLength};
        const QcPathResult result = m_finder.find(world, m_cells, rulesOf(mob), request);
        apply(mob, result, goal);
        m_queue.pop_front();

        budget -= result.nodes;
        m_nodesLastTick += result.nodes;
        ++m_searchesLastTick;
        ++m_searches;
    }
}

void QcPathPlanner::findQueued(const QList<QcCreature> &mobs)
{
    m_queued.clear();
    for (const quint32 id : m_queue) {
        m_queued.emplace_back(id, -1);
    }
    std::ranges::sort(m_queued);
    for (qsizetype i = 0; i < mobs.size(); ++i) {
        const auto found = std::ranges::lower_bound(m_queued, std::pair<quint32, qsizetype>(mobs[i].id, -1));
        if (found != m_queued.end() && found->first == mobs[i].id) {
            found->second = i;
        }
    }
}

qsizetype QcPathPlanner::queuedIndex(quint32 id) const
{
    const auto found = std::ranges::lower_bound(m_queued, std::pair<quint32, qsizetype>(id, -1));
    return found != m_queued.end() && found->first == id ? found->second : -1;
}

void QcPathPlanner::apply(QcCreature &mob, const QcPathResult &result, QcBlockPos goal) const
{
    QcMobPath &path = mob.path;
    path.queued = false;
    path.planned = true;
    path.cells = result.cells;
    path.next = 0;
    path.reachesGoal = result.reachesGoal;
    path.plannedGoal = goal;
    path.age = 0;
    path.stuckTicks = 0;
    path.closest = std::numeric_limits<double>::max();

    if (result.reachesGoal) {
        path.failures = 0;
        path.waitTicks = 0;
    } else {
        path.failures = std::min(path.failures + 1, 8);
        path.waitTicks = std::min(longestWaitTicks, firstWaitTicks << (path.failures - 1));
    }
}
