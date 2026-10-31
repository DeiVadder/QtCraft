// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

#include <vector>

/*! \brief Where a creature is going and the path it follows there (decision E-400).

    The behaviour sets the goal (QcMobBehaviour), QcMobNavigator follows the path and asks for a
    new one, QcPathPlanner finds it within the tick's budget. */
struct QcMobPath
{
    /*! \brief Why the creature goes to its goal. */
    enum class Purpose : quint8 {
        /*! No goal: the creature stands. */
        None,
        /*! It chases a target. */
        Chase,
        /*! It wanders. */
        Wander,
        /*! It flees. */
        Flee,
        /*! A villager going to its bed, workstation, meeting point or a crop (decision E-411);
            searched with the full budget, unlike a wanderer. */
        Errand
    };

    /*! Why the creature goes to the goal. */
    Purpose purpose = Purpose::None;
    /*! The goal point. */
    QcVec3 goal;
    /*! How close to the goal counts as there (cells). */
    int closeEnough = 0;

    /*! The path, as feet cells. */
    std::vector<QcBlockPos> cells;
    /*! The index in \c {cells} of the waypoint it walks to. */
    std::size_t next = 0;
    /*! A search ran for the goal cell \c {plannedGoal}; the path gets there or ends at the
        closest cell the search reached. */
    bool planned = false;
    /*! Whether the path gets to \c {plannedGoal}. */
    bool reachesGoal = false;
    /*! The goal cell the search ran for. */
    QcBlockPos plannedGoal;
    /*! Waiting in QcPathPlanner's queue. */
    bool queued = false;
    /*! Ticks since the path was planned. */
    int age = 0;
    /*! Ticks before it may ask again after a path that did not get there or got stuck; doubles
        with each such failure in a row (back-off), so an unreachable goal is not searched every
        tick. */
    int waitTicks = 0;
    /*! Failures in a row. */
    int failures = 0;
    /*! Progress towards the next waypoint: the closest it came. */
    double closest = 0.0;
    /*! The ticks since it came closer to the next waypoint. */
    int stuckTicks = 0;

    /*! Returns whether it has a goal. */
    bool hasGoal() const { return purpose != Purpose::None; }
    /*! Returns whether it walked the whole planned path. */
    bool finished() const { return planned && next >= cells.size(); }
    /*! Forgets the goal and the path (the creature stands or picks a new goal). */
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
