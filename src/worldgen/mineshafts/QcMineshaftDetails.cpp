// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/mineshafts/QcMineshaftDetails.h"

#include <algorithm>
#include <array>
#include <limits>

namespace {

// Purposes of the random values.
constexpr int SpawnerCount = 1;
constexpr int SpawnerCorridor = 2;
constexpr int Rails = 3;
constexpr int Cart = 4;
constexpr int CartPlace = 5;
constexpr int Light = 6;
constexpr int UpperWeb = 7;
constexpr int LowerWeb = 8;
constexpr int SpawnerWeb = 9;

// Share of the cells beside a spawner that get a cobweb, and of those in the middle lane above it.
constexpr double spawnerWebChance = 0.45;
constexpr double spawnerLaneWebChance = 0.25;

int randomIndex(int corridor, int along, int lateral, int height)
{
    return ((corridor * 64 + along) * 4 + lateral + 1) * 4 + height;
}

} // namespace

bool QcMineshaftDetails::Work::contains(QcBlockPos pos) const
{
    if (cells.contains(pos)) {
        return true;
    }
    return std::ranges::any_of(plan.rooms, [pos](const QcMineshaftPlan::Room &room) {
        const QcBlockPos low = room.placement.origin();
        return pos.x >= low.x && pos.x < low.x + room.placement.sizeX() && pos.z >= low.z && pos.z < low.z + room.placement.sizeZ() && pos.y >= low.y
               && pos.y < low.y + room.placement.structure().sizeY();
    });
}

QcMineshaftDetails::QcMineshaftDetails(const QcMineshaftSettings &settings, const QcMineshaftBlocks &blocks, bool carts)
    : m_settings(settings)
    , m_blocks(blocks)
    , m_carts(carts)
{
}

void QcMineshaftDetails::Work::place(QcBlockPos at, QcBlockId block)
{
    furnishing.blocks.append({at, block});
    cells.insert(at);
}

QcMineshaftDetails::Furnishing QcMineshaftDetails::furnish(const QcMineshaftPlan &plan, const Random &random) const
{
    Work work{plan, {}, {}};
    placeSpawners(work, random);
    placeRails(work, random);
    placeSupports(work);
    placeLights(work, random);
    placeCobwebs(work, random);
    return std::move(work.furnishing);
}

bool QcMineshaftDetails::isPlain(const QcMineshaftPlan &plan, const QcMineshaftPlan::Corridor &corridor)
{
    const auto isRoom = [&plan](int node) { return node == 0 || std::ranges::any_of(plan.rooms, [node](const auto &room) { return room.node == node; }); };
    return !corridor.stairs() && !isRoom(corridor.from) && !isRoom(corridor.to) && corridor.length >= 3;
}

int QcMineshaftDetails::distanceToSpawners(const QList<QcMineshaftPlan::Spawner> &spawners, QcBlockPos pos)
{
    const auto axis = [](int value, int low, int high) { return value < low ? low - value : (value > high ? value - high : 0); };
    int nearest = std::numeric_limits<int>::max();
    for (const QcMineshaftPlan::Spawner &spawner : spawners) {
        const int distance = axis(pos.x, spawner.areaLow.x, spawner.areaHigh.x) + axis(pos.y, spawner.areaLow.y, spawner.areaHigh.y)
                             + axis(pos.z, spawner.areaLow.z, spawner.areaHigh.z);
        nearest = std::min(nearest, distance);
    }
    return nearest;
}

void QcMineshaftDetails::placeSpawners(Work &work, const Random &random) const
{
    const QcMineshaftPlan &plan = work.plan;
    QList<int> candidates;
    for (qsizetype i = 0; i < plan.corridors.size(); ++i) {
        if (isPlain(plan, plan.corridors[i])) {
            candidates.append(static_cast<int>(i));
        }
    }
    const int span = m_settings.maxSpawners - m_settings.minSpawners + 1;
    const int count = m_settings.minSpawners + std::min(span - 1, static_cast<int>(random(SpawnerCount, 0) * span));
    for (int i = 0; i < count && !candidates.isEmpty(); ++i) {
        const qsizetype pick = std::min(candidates.size() - 1, static_cast<qsizetype>(random(SpawnerCorridor, i) * static_cast<double>(candidates.size())));
        const QcMineshaftPlan::Corridor &corridor = plan.corridors[candidates.takeAt(pick)];
        const QcBlockPos at = spawnerCell(corridor);
        work.place(at, m_blocks.spawner);
        work.addSpawner({at, at.offset(-m_blocks.areaX, -m_blocks.areaY, -m_blocks.areaZ), at.offset(m_blocks.areaX, m_blocks.areaY, m_blocks.areaZ)});
        placeSpawnerWebs(work, corridor, random);
    }
}

QcBlockPos QcMineshaftDetails::spawnerCell(const QcMineshaftPlan::Corridor &corridor)
{
    // Plain corridors are three cells long or more, so a cell beside a support lies within it.
    const int middle = corridor.length / 2;
    const int along = !corridor.supports.contains(middle) ? middle : (middle + 1 < corridor.length ? middle + 1 : middle - 1);
    return corridor.cell(along, 1);
}

void QcMineshaftDetails::placeSpawnerWebs(Work &work, const QcMineshaftPlan::Corridor &corridor, const Random &random) const
{
    // Beside the spawner from the floor up, above it in the middle lane; not in supports.
    constexpr std::array<std::pair<int, int>, 8> cells{{{-1, 0}, {-1, 1}, {-1, 2}, {1, 0}, {1, 1}, {1, 2}, {0, 1}, {0, 2}}};
    const int middle = corridor.length / 2;
    const int index = static_cast<int>(work.furnishing.spawners.size());
    for (int along = std::max(0, middle - 2); along <= std::min(corridor.length - 1, middle + 2); ++along) {
        if (corridor.supports.contains(along)) {
            continue;
        }
        for (const auto &[lateral, height] : cells) {
            const QcBlockPos at = corridor.cell(along, lateral).offset(0, height, 0);
            const double chance = lateral == 0 ? spawnerLaneWebChance : spawnerWebChance;
            if (!work.contains(at) && random(SpawnerWeb, randomIndex(index, along, lateral, height)) < chance) {
                work.place(at, m_blocks.cobweb);
            }
        }
    }
}

void QcMineshaftDetails::placeRails(Work &work, const Random &random) const
{
    const QcMineshaftPlan &plan = work.plan;
    for (qsizetype i = 0; i < plan.corridors.size(); ++i) {
        const QcMineshaftPlan::Corridor &corridor = plan.corridors[i];
        const bool spawner = std::ranges::any_of(work.furnishing.spawners, [&corridor](const auto &s) { return s.at == spawnerCell(corridor); });
        if (!isPlain(plan, corridor) || spawner || random(Rails, static_cast<int>(i)) >= m_settings.railChance) {
            continue;
        }

        work.addRails(static_cast<int>(i));
        const QcBlockId rail = corridor.dx != 0 ? m_blocks.railEastWest : m_blocks.railNorthSouth;
        for (int along = 0; along < corridor.length; ++along) {
            work.place(corridor.cell(along), rail);
        }
        if (m_carts && random(Cart, static_cast<int>(i)) < m_settings.cartChance) {
            const int along = std::min(corridor.length - 1, static_cast<int>(random(CartPlace, static_cast<int>(i)) * corridor.length));
            work.addCart(corridor.cell(along));
        }
    }
}

void QcMineshaftDetails::placeSupports(Work &work) const
{
    const QcMineshaftPlan &plan = work.plan;
    for (const QcMineshaftPlan::Corridor &corridor : plan.corridors) {
        for (const int along : corridor.supports) {
            placeSupport(work, corridor, along);
        }
    }
    for (const QcMineshaftPlan::Node &node : std::as_const(plan.nodes)) {
        if (node.bridge) {
            work.addPillar(node.centre.offset(0, -1, 0));
        }
    }
}

void QcMineshaftDetails::placeSupport(Work &work, const QcMineshaftPlan::Corridor &corridor, int along) const
{
    if (work.contains(corridor.cell(along, -1)) || work.contains(corridor.cell(along, 1))) {
        return;
    }
    for (const int lateral : {-1, 1}) {
        work.place(corridor.cell(along, lateral), m_blocks.post);
        work.place(corridor.cell(along, lateral).offset(0, 1, 0), m_blocks.post);
        if (corridor.bridge) {
            work.addPillar(corridor.cell(along, lateral).offset(0, -1, 0));
        }
    }
    for (int lateral = -1; lateral <= 1; ++lateral) {
        work.place(corridor.cell(along, lateral).offset(0, 2, 0), m_blocks.planks);
    }
}

void QcMineshaftDetails::placeLights(Work &work, const Random &random) const
{
    const QcMineshaftPlan &plan = work.plan;
    for (qsizetype i = 0; i < plan.corridors.size(); ++i) {
        const QcMineshaftPlan::Corridor &corridor = plan.corridors[i];
        for (const int along : corridor.supports) {
            // Beside the beam, hanging on it: after it, or before it at the corridor's end.
            const int at = along + 1 < corridor.length ? along + 1 : along - 1;
            const QcBlockPos cell = corridor.cell(at).offset(0, 2, 0);
            const QcBlockPos beam = corridor.cell(along).offset(0, 2, 0);
            // A support left out (in a room) has no beam.
            const bool hung = work.cells.contains(beam);
            if (at < 0 || !hung || work.contains(cell) || random(Light, static_cast<int>(i) * 64 + along) >= m_settings.lightChance
                || distanceToSpawners(work.furnishing.spawners, cell) < m_settings.lightDistance) {
                continue;
            }
            work.place(cell, m_blocks.lights[static_cast<std::size_t>(QcMineshaftBlocks::sideOf(beam.x - cell.x, beam.z - cell.z))]);
        }
    }
}

void QcMineshaftDetails::placeCobwebs(Work &work, const Random &random) const
{
    for (qsizetype i = 0; i < work.plan.corridors.size(); ++i) {
        placeCobwebsIn(work, static_cast<int>(i), random);
    }
}

void QcMineshaftDetails::placeCobwebsIn(Work &work, int index, const Random &random) const
{
    // Both sides, in the upper corners more often than lower down.
    constexpr std::array<std::pair<int, int>, 6> cells{{{-1, 0}, {-1, 1}, {-1, 2}, {1, 0}, {1, 1}, {1, 2}}};
    const QcMineshaftPlan::Corridor &corridor = work.plan.corridors[index];
    for (int along = 0; along < corridor.length; ++along) {
        for (const auto &[lateral, height] : cells) {
            const QcBlockPos at = corridor.cell(along, lateral).offset(0, height, 0);
            const double chance = height == 2 ? m_settings.cobwebChance : m_settings.cobwebChance / 4;
            const int purpose = height == 2 ? UpperWeb : LowerWeb;
            if (!work.contains(at) && random(purpose, randomIndex(index, along, lateral, height)) < chance) {
                work.place(at, m_blocks.cobweb);
            }
        }
    }
}
