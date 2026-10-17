#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

#include <vector>

// Where a creature is going and the path it follows there (decision E-400). The behaviour sets the
// goal (QcMobBehaviour), QcMobNavigator follows the path and asks for a new one, QcPathPlanner
// finds it within the tick's budget.
struct QcMobPath
{
    // Errand: a villager going to its bed, workstation, meeting point or a crop (decision E-411);
    // searched with the full budget, unlike a wanderer.
    enum class Purpose : quint8 { None, Chase, Wander, Flee, Errand };

    // The goal: a point, why the creature goes there, and how close counts as there (cells).
    Purpose purpose = Purpose::None;
    QcVec3 goal;
    int closeEnough = 0;

    // The path, as feet cells; `next` is the waypoint it walks to.
    std::vector<QcBlockPos> cells;
    std::size_t next = 0;
    // A search ran for the goal cell `plannedGoal`; the path gets there or ends at the closest cell
    // the search reached.
    bool planned = false;
    bool reachesGoal = false;
    QcBlockPos plannedGoal;
    // Waiting in QcPathPlanner's queue.
    bool queued = false;
    // Ticks since the path was planned.
    int age = 0;
    // Ticks before it may ask again after a path that did not get there or got stuck; doubles with
    // each such failure in a row (back-off), so an unreachable goal is not searched every tick.
    int waitTicks = 0;
    int failures = 0;
    // Progress towards the next waypoint: the closest it came and the ticks since it came closer.
    double closest = 0.0;
    int stuckTicks = 0;

    bool hasGoal() const { return purpose != Purpose::None; }
    bool finished() const { return planned && next >= cells.size(); }
    // Forgets the goal and the path (the creature stands or picks a new goal).
    void clear()
    {
        purpose = Purpose::None;
        cells.clear();
        next = 0;
        planned = false;
        reachesGoal = false;
        stuckTicks = 0;
    }
};
