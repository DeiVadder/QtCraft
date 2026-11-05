// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "common/physics/QcPhysics.h"

#include <QRandomGenerator>

#include <bitset>
#include <vector>

class QcBlockAccess;
struct QcCreature;

/*! \brief Routes of creatures that swim (decisions E-502, E-972), like the swimming
    goals of the role model: a goal only in a cell of water where the whole body has room
    (QcSwimSpace::fits), reached through such cells.

    A route is planned by a flood through the water around the creature, cell by cell across
    faces, within \c {reach} cells sideways and \c {depthReach} up and down; the goal is drawn from
    the cells found (a fleeing creature takes the one farthest along its way away). The route is
    kept in the creature's QcMobPath: \c {purpose} Wander or Flee, \c {goal} the goal's feet and
    \c {cells} the cells to swim through, of which the creature heads for the farthest it sees in
    a straight line. In a pool too small to swim in, the route leads to the middle of the
    creature's own cell, where it idles. */
class QcSwimRoutes
{
public:
    /*! How far a route reaches, in cells sideways and up or down. \qcshared */
    static constexpr int reach = 6;
    static constexpr int depthReach = 3;
    /*! Most cells one flood looks at. */
    static constexpr int maxCells = 512;
    /*! A wandering goal lies at least this far away, in metres, where the water allows. */
    static constexpr double minGoalDistance = 1.5;
    /*! A waypoint this close to the feet, in metres, counts as reached. */
    static constexpr double waypointReach = 0.3;
    /*! How many waypoints ahead a creature looks for one it can swim to in a straight line. */
    static constexpr int lookAhead = 6;

    /*! Creates the routes; the goals are drawn from \a random. */
    explicit QcSwimRoutes(QRandomGenerator &random);

    /*! Plans a new route of \a mob, a body of \a body, in \a world: a wandering one, or one away
        from danger towards \a away when that is not zero. Returns whether it found any (none
        where its body has no room at all; its path is then cleared). */
    bool plan(QcCreature &mob, QcPhysics::BodySize body, const QcBlockAccess &world, const QcVec3 &away);
    /*! Returns the way from the feet of \a mob, a body of \a body in \a world, to its next
        waypoint, after passing the waypoints it reached and skipping those it can cut across
        to; zero once the route is done (QcMobPath::finished). */
    QcVec3 towardsNext(QcCreature &mob, QcPhysics::BodySize body, const QcBlockAccess &world);

private:
    // A cell the flood reached and the index of the cell it came from (-1 for the first).
    struct Reached
    {
        QcBlockPos cell;
        qsizetype from = -1;
    };

    // Fills m_reached with the cells of water around `start` where `body` has room.
    void flood(QcBlockPos start, QcPhysics::BodySize body, const QcBlockAccess &world);
    // The index in m_reached of the goal for feet at `feet`, -1 for none.
    qsizetype choose(const QcVec3 &feet, QcPhysics::BodySize body, const QcVec3 &away, const QcBlockAccess &world);
    // The cells of the window around a flood's start, one bit each.
    static constexpr int windowCells = (2 * reach + 1) * (2 * reach + 1) * (2 * depthReach + 1);
    // The bit of the cell `cell` in a flood from `start`; the cell lies within the window.
    static std::size_t bitOf(QcBlockPos cell, QcBlockPos start);
    // Moves the next waypoint of `mob` to the farthest of the next lookAhead it reaches straight.
    static void cutAcross(QcCreature &mob, QcPhysics::BodySize body, const QcBlockAccess &world);

    QRandomGenerator &m_random;
    // NOLINT(qc-member-access): scratch of one flood, kept from plan to plan for its storage.
    std::vector<Reached> m_reached;
    // NOLINT(qc-member-access): scratch of one flood, the cells of the window it looked at.
    std::bitset<windowCells> m_seen;
};
