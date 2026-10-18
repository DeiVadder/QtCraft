#include "worldgen/mineshafts/QcMineshaftDetails.h"

#include <algorithm>
#include <array>
#include <limits>

namespace {

// Purposes of the random values.
enum Purpose : int {
    SpawnerCount = 1,
    SpawnerCorridor,
    Rails,
    Cart,
    CartPlace,
    Light,
    UpperWeb,
    LowerWeb,
    SpawnerWeb,
};

// Share of the cells beside a spawner that get a cobweb, and of those in the middle lane above it.
constexpr double spawnerWebChance = 0.45;
constexpr double spawnerLaneWebChance = 0.25;

int randomIndex(int corridor, int along, int lateral, int height)
{
    return ((corridor * 64 + along) * 4 + lateral + 1) * 4 + height;
}

} // namespace

bool QcMineshaftDetails::Taken::contains(QcBlockPos pos) const
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

void QcMineshaftDetails::add(QcMineshaftPlan &plan, const Random &random) const
{
    Taken taken{plan, {}};
    placeSpawners(plan, taken, random);
    placeRails(plan, taken, random);
    placeSupports(plan, taken);
    placeLights(plan, taken, random);
    placeCobwebs(plan, taken, random);
}

bool QcMineshaftDetails::isPlain(const QcMineshaftPlan &plan, const QcMineshaftPlan::Corridor &corridor)
{
    const auto isRoom = [&plan](int node) { return node == 0 || std::ranges::any_of(plan.rooms, [node](const auto &room) { return room.node == node; }); };
    return !corridor.stairs() && !isRoom(corridor.from) && !isRoom(corridor.to) && corridor.length >= 3;
}

void QcMineshaftDetails::place(QcMineshaftPlan &plan, Taken &taken, QcBlockPos at, QcBlockId block)
{
    plan.blocks.append({at, block});
    taken.cells.insert(at);
}

int QcMineshaftDetails::distanceToSpawners(const QcMineshaftPlan &plan, QcBlockPos pos)
{
    const auto axis = [](int value, int low, int high) { return value < low ? low - value : (value > high ? value - high : 0); };
    int nearest = std::numeric_limits<int>::max();
    for (const QcMineshaftPlan::Spawner &spawner : plan.spawners) {
        const int distance = axis(pos.x, spawner.areaLow.x, spawner.areaHigh.x) + axis(pos.y, spawner.areaLow.y, spawner.areaHigh.y)
                             + axis(pos.z, spawner.areaLow.z, spawner.areaHigh.z);
        nearest = std::min(nearest, distance);
    }
    return nearest;
}

void QcMineshaftDetails::placeSpawners(QcMineshaftPlan &plan, Taken &taken, const Random &random) const
{
    QList<int> candidates;
    for (qsizetype i = 0; i < plan.corridors.size(); ++i) {
        if (isPlain(plan, plan.corridors[i])) {
            candidates.append(int(i));
        }
    }
    const int span = m_settings.maxSpawners - m_settings.minSpawners + 1;
    const int count = m_settings.minSpawners + std::min(span - 1, int(random(SpawnerCount, 0) * span));
    for (int i = 0; i < count && !candidates.isEmpty(); ++i) {
        const qsizetype pick = std::min(candidates.size() - 1, qsizetype(random(SpawnerCorridor, i) * double(candidates.size())));
        const QcMineshaftPlan::Corridor &corridor = plan.corridors[candidates.takeAt(pick)];
        const QcBlockPos at = spawnerCell(corridor);
        place(plan, taken, at, m_blocks.spawner);
        plan.spawners.append({at, at.offset(-m_blocks.areaX, -m_blocks.areaY, -m_blocks.areaZ), at.offset(m_blocks.areaX, m_blocks.areaY, m_blocks.areaZ)});
        placeSpawnerWebs(plan, taken, corridor, random);
    }
}

QcBlockPos QcMineshaftDetails::spawnerCell(const QcMineshaftPlan::Corridor &corridor)
{
    // Plain corridors are three cells long or more, so a cell beside a support lies within it.
    const int middle = corridor.length / 2;
    const int along = !corridor.supports.contains(middle) ? middle : (middle + 1 < corridor.length ? middle + 1 : middle - 1);
    return corridor.cell(along, 1);
}

void QcMineshaftDetails::placeSpawnerWebs(QcMineshaftPlan &plan, Taken &taken, const QcMineshaftPlan::Corridor &corridor, const Random &random) const
{
    // Beside the spawner from the floor up, above it in the middle lane; not in supports.
    constexpr std::array<std::pair<int, int>, 8> cells{{{-1, 0}, {-1, 1}, {-1, 2}, {1, 0}, {1, 1}, {1, 2}, {0, 1}, {0, 2}}};
    const int middle = corridor.length / 2;
    const int index = int(plan.spawners.size());
    for (int along = std::max(0, middle - 2); along <= std::min(corridor.length - 1, middle + 2); ++along) {
        if (corridor.supports.contains(along)) {
            continue;
        }
        for (const auto &[lateral, height] : cells) {
            const QcBlockPos at = corridor.cell(along, lateral).offset(0, height, 0);
            const double chance = lateral == 0 ? spawnerLaneWebChance : spawnerWebChance;
            if (!taken.contains(at) && random(SpawnerWeb, randomIndex(index, along, lateral, height)) < chance) {
                place(plan, taken, at, m_blocks.cobweb);
            }
        }
    }
}

void QcMineshaftDetails::placeRails(QcMineshaftPlan &plan, Taken &taken, const Random &random) const
{
    for (qsizetype i = 0; i < plan.corridors.size(); ++i) {
        QcMineshaftPlan::Corridor &corridor = plan.corridors[i];
        const bool spawner = std::ranges::any_of(plan.spawners, [&corridor](const auto &s) { return s.at == spawnerCell(corridor); });
        if (!isPlain(plan, corridor) || spawner || random(Rails, int(i)) >= m_settings.railChance) {
            continue;
        }

        corridor.rails = true;
        const QcBlockId rail = corridor.dx != 0 ? m_blocks.railEastWest : m_blocks.railNorthSouth;
        for (int along = 0; along < corridor.length; ++along) {
            place(plan, taken, corridor.cell(along), rail);
        }
        if (m_carts && random(Cart, int(i)) < m_settings.cartChance) {
            const int along = std::min(corridor.length - 1, int(random(CartPlace, int(i)) * corridor.length));
            plan.carts.append(corridor.cell(along));
        }
    }
}

void QcMineshaftDetails::placeSupports(QcMineshaftPlan &plan, Taken &taken) const
{
    for (qsizetype i = 0; i < plan.corridors.size(); ++i) {
        for (const int along : std::as_const(plan.corridors[i].supports)) {
            placeSupport(plan, taken, plan.corridors[i], along);
        }
    }
    for (const QcMineshaftPlan::Node &node : std::as_const(plan.nodes)) {
        if (node.bridge) {
            plan.pillars.append(node.centre.offset(0, -1, 0));
        }
    }
}

void QcMineshaftDetails::placeSupport(QcMineshaftPlan &plan, Taken &taken, const QcMineshaftPlan::Corridor &corridor, int along) const
{
    if (taken.contains(corridor.cell(along, -1)) || taken.contains(corridor.cell(along, 1))) {
        return;
    }
    for (const int lateral : {-1, 1}) {
        place(plan, taken, corridor.cell(along, lateral), m_blocks.post);
        place(plan, taken, corridor.cell(along, lateral).offset(0, 1, 0), m_blocks.post);
        if (corridor.bridge) {
            plan.pillars.append(corridor.cell(along, lateral).offset(0, -1, 0));
        }
    }
    for (int lateral = -1; lateral <= 1; ++lateral) {
        place(plan, taken, corridor.cell(along, lateral).offset(0, 2, 0), m_blocks.planks);
    }
}

void QcMineshaftDetails::placeLights(QcMineshaftPlan &plan, Taken &taken, const Random &random) const
{
    for (qsizetype i = 0; i < plan.corridors.size(); ++i) {
        const QcMineshaftPlan::Corridor &corridor = plan.corridors[i];
        for (const int along : corridor.supports) {
            // Beside the beam, hanging on it: after it, or before it at the corridor's end.
            const int at = along + 1 < corridor.length ? along + 1 : along - 1;
            const QcBlockPos cell = corridor.cell(at).offset(0, 2, 0);
            const QcBlockPos beam = corridor.cell(along).offset(0, 2, 0);
            // A support left out (in a room) has no beam.
            const bool hung = taken.cells.contains(beam);
            if (at < 0 || !hung || taken.contains(cell) || random(Light, int(i) * 64 + along) >= m_settings.lightChance
                || distanceToSpawners(plan, cell) < m_settings.lightDistance) {
                continue;
            }
            place(plan, taken, cell, m_blocks.lights[std::size_t(QcMineshaftBlocks::sideOf(beam.x - cell.x, beam.z - cell.z))]);
        }
    }
}

void QcMineshaftDetails::placeCobwebs(QcMineshaftPlan &plan, Taken &taken, const Random &random) const
{
    for (qsizetype i = 0; i < plan.corridors.size(); ++i) {
        placeCobwebsIn(plan, taken, int(i), random);
    }
}

void QcMineshaftDetails::placeCobwebsIn(QcMineshaftPlan &plan, Taken &taken, int index, const Random &random) const
{
    // Both sides, in the upper corners more often than lower down.
    constexpr std::array<std::pair<int, int>, 6> cells{{{-1, 0}, {-1, 1}, {-1, 2}, {1, 0}, {1, 1}, {1, 2}}};
    const QcMineshaftPlan::Corridor corridor = plan.corridors[index];
    for (int along = 0; along < corridor.length; ++along) {
        for (const auto &[lateral, height] : cells) {
            const QcBlockPos at = corridor.cell(along, lateral).offset(0, height, 0);
            const double chance = height == 2 ? m_settings.cobwebChance : m_settings.cobwebChance / 4;
            const int purpose = height == 2 ? UpperWeb : LowerWeb;
            if (!taken.contains(at) && random(purpose, randomIndex(index, along, lateral, height)) < chance) {
                place(plan, taken, at, m_blocks.cobweb);
            }
        }
    }
}
