// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/pathing/QcSwimRoutes.h"

#include "server/entities/QcCreature.h"
#include "server/pathing/QcSwimSpace.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>

QcSwimRoutes::QcSwimRoutes(QRandomGenerator &random)
    : m_random(random)
{
}

std::size_t QcSwimRoutes::bitOf(QcBlockPos cell, QcBlockPos start)
{
    const int x = cell.x - start.x + reach;
    const int z = cell.z - start.z + reach;
    const int y = cell.y - start.y + depthReach;
    const int index = (x * (2 * reach + 1) + z) * (2 * depthReach + 1) + y;
    return static_cast<std::size_t>(index);
}

void QcSwimRoutes::flood(QcBlockPos start, QcPhysics::BodySize body, const QcBlockAccess &world)
{
    static constexpr QcBlockPos steps[] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}};
    m_reached.clear();
    m_seen.reset();
    m_reached.push_back({start, -1});
    m_seen.set(bitOf(start, start));

    // Breadth first across the faces of the cells: the route from each cell back to the start
    // passes only cells with room for the body.
    for (std::size_t i = 0; i < m_reached.size() && std::ssize(m_reached) < maxCells; ++i) {
        const QcBlockPos here = m_reached[i].cell;
        for (const QcBlockPos &step : steps) {
            const QcBlockPos next = here.offset(step.x, step.y, step.z);
            const bool within = std::abs(next.x - start.x) <= reach && std::abs(next.z - start.z) <= reach && std::abs(next.y - start.y) <= depthReach;
            if (!within || m_seen.test(bitOf(next, start))) {
                continue;
            }
            m_seen.set(bitOf(next, start));
            if (QcSwimSpace::fits(world, QcSwimSpace::pointIn(next, body), body)) {
                m_reached.push_back({next, static_cast<qsizetype>(i)});
            }
        }
    }
}

qsizetype QcSwimRoutes::choose(const QcVec3 &feet, QcPhysics::BodySize body, const QcVec3 &away, const QcBlockAccess &world)
{
    const auto offsetOf = [&](qsizetype i) { return QcSwimSpace::pointIn(m_reached[static_cast<std::size_t>(i)].cell, body) - feet; };
    const qsizetype count = std::ssize(m_reached);

    // Fleeing: the cell farthest along the way away.
    if (away.length() > 0.0) {
        const QcVec3 way = away.normalized();
        qsizetype best = -1;
        double farthest = 0.0;
        for (qsizetype i = 1; i < count; ++i) {
            if (const double along = offsetOf(i).dot(way); along > farthest) {
                farthest = along;
                best = i;
            }
        }
        if (best >= 0) {
            return best;
        }
    }

    // Wandering: any cell at a distance, else any other cell, else the middle of its own.
    for (const double least : {minGoalDistance, 0.0}) {
        const auto fitting = [&](qsizetype i) { return offsetOf(i).length() >= least; };
        qsizetype candidates = 0;
        for (qsizetype i = 1; i < count; ++i) {
            candidates += fitting(i) ? 1 : 0;
        }
        qsizetype drawn = candidates > 0 ? static_cast<qsizetype>(m_random.bounded(static_cast<quint32>(candidates))) : -1;
        for (qsizetype i = 1; i < count && drawn >= 0; ++i) {
            if (fitting(i) && drawn-- == 0) {
                return i;
            }
        }
    }
    return QcSwimSpace::fits(world, QcSwimSpace::pointIn(m_reached.front().cell, body), body) ? 0 : -1;
}

bool QcSwimRoutes::plan(QcCreature &mob, QcPhysics::BodySize body, const QcBlockAccess &world, const QcVec3 &away)
{
    const QcVec3 &feet = mob.motion.position;
    flood(QcPhysics::blockContaining(feet + QcVec3{0.0, body.height * 0.5, 0.0}), body, world);
    const qsizetype goal = choose(feet, body, away, world);
    QcMobPath &path = mob.path;
    path.clear();
    if (goal < 0) {
        return false;
    }

    // The cells from the goal back to the start, then turned around; the start itself only when
    // it is the goal.
    for (qsizetype i = goal; i > 0; i = m_reached[static_cast<std::size_t>(i)].from) {
        path.cells.push_back(m_reached[static_cast<std::size_t>(i)].cell);
    }
    if (goal == 0) {
        path.cells.push_back(m_reached.front().cell);
    }
    std::ranges::reverse(path.cells);

    path.purpose = away.length() > 0.0 ? QcMobPath::Purpose::Flee : QcMobPath::Purpose::Wander;
    path.plannedGoal = path.cells.back();
    path.goal = QcSwimSpace::pointIn(path.plannedGoal, body);
    path.planned = true;
    path.reachesGoal = true;
    cutAcross(mob, body, world);
    return true;
}

void QcSwimRoutes::cutAcross(QcCreature &mob, QcPhysics::BodySize body, const QcBlockAccess &world)
{
    QcMobPath &path = mob.path;
    const std::size_t end = std::min(path.next + static_cast<std::size_t>(lookAhead) + 1, path.cells.size());
    for (std::size_t i = end; i-- > path.next + 1;) {
        if (QcSwimSpace::clearLine(world, mob.motion.position, QcSwimSpace::pointIn(path.cells[i], body), body)) {
            path.next = i;
            return;
        }
    }
}

QcVec3 QcSwimRoutes::towardsNext(QcCreature &mob, QcPhysics::BodySize body, const QcBlockAccess &world)
{
    QcMobPath &path = mob.path;
    while (path.next < path.cells.size()) {
        const QcVec3 way = QcSwimSpace::pointIn(path.cells[path.next], body) - mob.motion.position;
        if (way.length() > waypointReach) {
            return way;
        }
        ++path.next;
        cutAcross(mob, body, world);
    }
    return {};
}
