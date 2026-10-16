#pragma once

#include "QcPathCells.h"
#include "QcPathFinder.h"
#include "QcServerLimits.h"

#include <QList>

#include <deque>
#include <functional>
#include <utility>
#include <vector>

class QcBlockAccess;
class QcContentRegistry;
struct QcCreature;

// Path searches of all creatures within a budget per tick (decision E-400). Creatures ask for a
// path (QcMobNavigator); at the start of the next tick the waiting searches run in the order they
// were asked for until the next would overrun the tick's node budget (QcServerLimits
// pathNodesPerTick); the rest wait for the next tick at the front of the queue, so every creature gets
// its turn. A search expands at most pathNodesPerSearch nodes (a wanderer a quarter of that) and a
// path keeps at most pathLength waypoints. A budget of 0 switches path finding off: creatures then
// walk straight at their goals.
class QcPathPlanner
{
public:
    explicit QcPathPlanner(const QcContentRegistry &content);

    void setLimits(qint64 nodesPerTick, qint64 nodesPerSearch, qint64 pathLength);
    bool enabled() const { return m_nodesPerTick > 0; }
    const QcPathCellTable &cells() const { return m_cells; }

    // Queues a search for the creature's goal (QcMobPath) unless one is waiting.
    void request(QcCreature &mob);
    // Runs the waiting searches within this tick's budget. `rulesOf` gives the rules of a creature's kind.
    void serve(const QcBlockAccess &world, QList<QcCreature> &mobs, const std::function<QcPathRules(const QcCreature &)> &rulesOf);

    // Nodes expanded in the last tick, searches run in it and waiting after it (tests, debug).
    int nodesLastTick() const { return m_nodesLastTick; }
    int searchesLastTick() const { return m_searchesLastTick; }
    qsizetype waiting() const { return qsizetype(m_queue.size()); }
    // Searches run since the start (tests).
    qint64 searches() const { return m_searches; }

private:
    // Nodes the search of a creature may expand.
    int nodesFor(const QcCreature &mob) const;
    void apply(QcCreature &mob, const QcPathResult &result, QcBlockPos goal) const;
    // Finds the waiting creatures in `mobs` (one pass, without allocating once warmed up), and the
    // index of one of them afterwards (-1: gone).
    void findQueued(const QList<QcCreature> &mobs);
    qsizetype queuedIndex(quint32 id) const;

    QcPathCellTable m_cells;
    QcPathFinder m_finder;
    std::deque<quint32> m_queue;
    // The waiting creatures as (id, index in the list), sorted by id.
    std::vector<std::pair<quint32, qsizetype>> m_queued;
    int m_nodesPerTick = int(QcServerLimits{}.pathNodesPerTick);
    int m_nodesPerSearch = int(QcServerLimits{}.pathNodesPerSearch);
    int m_pathLength = int(QcServerLimits{}.pathLength);
    int m_nodesLastTick = 0;
    int m_searchesLastTick = 0;
    qint64 m_searches = 0;
};
