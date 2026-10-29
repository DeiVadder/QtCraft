// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

#include <QHash>
#include <QList>

#include <array>
#include <memory>
#include <optional>
#include <vector>

struct QcVillagePlan;

// Long walks through a village (decision E-411): a path search covers only 64 x 64 cells around the
// walker (QcPathWindow, decision E-400), so a villager whose goal lies farther first walks to the
// cell of the village's roads that brings it closest to the goal along the roads, and from there on
// (legs). The roads are a coarse graph: the cells above the road, branch road and doorstep cells and
// the boardwalks and jetties of the plan (QcVillagePlan), each joined to its eight neighbours at
// most one block higher or lower; the distance along them comes from a breadth-first search from
// the road cell nearest the goal. The graphs of the villages last used are kept.
class QcVillagerRoads
{
public:
    // Goals within this many cells (on both horizontal axes) are walked to directly.
    static constexpr int legReach = 24;
    // Road cells farther than this above or below the walker are no leg.
    static constexpr int legHeight = 12;
    static constexpr int maxGraphs = 32;

    // The next leg from `from` towards `to` in the village of `plan`: the stand cell on its roads to
    // walk to first. None when `to` lies within legReach or the roads bring the walker no closer.
    std::optional<QcBlockPos> nextLeg(const QcVillagePlan &plan, QcBlockPos from, QcBlockPos to);
    // The way out of the water for a villager at `from` that found no path from there (fallen off
    // a boardwalk, under its own house): the nearest road cell on land (not a boardwalk or jetty)
    // within legHeight above or below. None without one.
    std::optional<QcBlockPos> wayOutOfWater(const QcVillagePlan &plan, QcBlockPos from);
    // Legs planned since the start (tests, measurements).
    qint64 planned() const { return m_planned; }

private:
    struct Graph
    {
        // The road cells on land first (roads, branch roads, doorsteps), then the boardwalks.
        std::vector<QcBlockPos> cells;
        std::size_t landCells = 0;
        // Up to eight neighbours per cell, -1 for none.
        std::vector<std::array<int, 8>> neighbours;
        // Distances of the last search and its scratch queue.
        std::vector<int> distance;
        std::vector<int> queue;

        // Fills `distance` with the steps from `start` along the roads (-1: not reachable).
        void measureFrom(int start);
    };

    Graph &graphOf(const QcVillagePlan &plan);
    static std::shared_ptr<Graph> build(const QcVillagePlan &plan);
    // The cell nearest to `pos` (horizontally, then vertically), -1 for an empty graph. A walker's
    // own cell (`walker`) is one it can step onto, at most a block higher or lower, if there is one
    // at all: not the boardwalk overhead of a villager in the water below it.
    static int nearest(const Graph &graph, QcBlockPos pos, bool walker);

    QHash<qint64, std::shared_ptr<Graph>> m_graphs;
    // Villages by last use, the oldest first.
    QList<qint64> m_used;
    qint64 m_planned = 0;
};
