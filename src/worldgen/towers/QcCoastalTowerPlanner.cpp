#include "worldgen/towers/QcCoastalTowerPlanner.h"

#include "worldgen/mineshafts/QcMineshaftGenerator.h"
#include "worldgen/surface/QcSurfaceStructures.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <QSet>

#include <algorithm>
#include <array>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

// Purposes of the random values.
enum Purpose : int {
    Chance = 1,
    KindPick,
    Order,
    Turn,
    Priority,
};

// Layers of ground under a footprint's columns that must not be cave.
constexpr int solidLayers = 3;

} // namespace

QcCoastalTowerPlanner::QcCoastalTowerPlanner(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed)
    : m_terrain(terrain)
    , m_content(content)
    , m_settings(content.worldgen().terrainSettings().coastalTowers)
    , m_seed(qcMixSeed(seed, qcNameSalt(u"coastal_towers"_s)))
{
    if (m_settings.enabled) {
        resolve(content);
    }
}

void QcCoastalTowerPlanner::resolve(const QcContentRegistry &content)
{
    const std::optional<QcBlockId> foundation = content.stateId(m_settings.foundation);
    if (!foundation) {
        m_warnings.append(u"coastalTowers: unknown foundation block '%1', no towers"_s.arg(m_settings.foundation));
        return;
    }
    m_foundation = *foundation;

    // A tower must stay inside its region with a column to spare on each side.
    for (const QcCoastalTowerSettings::Structure &entry : std::as_const(m_settings.structures)) {
        const QcStructureDefinition *definition = content.worldgen().structure(entry.structure);
        QcStructure::Loaded structure = definition ? QcStructure::fromDefinition(*definition, content)
                                                   : std::unexpected(QcStructure::Failure{u"unknown"_s, {}});
        m_warnings.append(QcStructure::warningsOf(structure));
        if (!structure || std::max(structure->sizeX(), structure->sizeZ()) + 2 > m_settings.regionSize) {
            m_warnings.append(u"coastalTowers: structure '%1' is unknown or does not fit the region, left out"_s.arg(entry.structure));
            continue;
        }
        m_kinds.append({std::make_shared<const QcStructure>(std::move(*structure)), entry.weight});
        m_totalWeight += entry.weight;
    }
    m_enabled = !m_kinds.isEmpty();
}

double QcCoastalTowerPlanner::random(int regionX, int regionZ, int purpose, int index) const
{
    return qcPositionRandom(qcMixSeed(m_seed, static_cast<quint64>(purpose)), regionX, index, regionZ);
}

double QcCoastalTowerPlanner::priority(int regionX, int regionZ) const
{
    return random(regionX, regionZ, Priority);
}

const QcCoastalTowerPlanner::Kind &QcCoastalTowerPlanner::pick(double random) const
{
    int left = static_cast<int>(random * m_totalWeight);
    const auto chosen = std::ranges::find_if(m_kinds, [&left](const Kind &kind) {
        left -= kind.weight;
        return left < 0;
    });
    return chosen != m_kinds.end() ? *chosen : m_kinds.constLast();
}

std::shared_ptr<const QcCoastalTowerPlan> QcCoastalTowerPlanner::plan(int regionX, int regionZ) const
{
    if (!m_enabled || random(regionX, regionZ, Chance) >= m_settings.chance) {
        return nullptr;
    }

    const Kind &kind = pick(random(regionX, regionZ, KindPick));
    const int margin = std::max(kind.structure->sizeX(), kind.structure->sizeZ()) / 2 + 1;
    const QList<std::pair<int, int>> sites = candidates(regionX, regionZ, margin);
    const qsizetype tries = std::min<qsizetype>(sites.size(), m_settings.attempts);
    for (qsizetype i = 0; i < tries; ++i) {
        const auto [x, z] = sites[i];
        if (std::optional<QcCoastalTowerPlan> site = siteAt(kind, x, z, random(regionX, regionZ, Turn, static_cast<int>(i)))) {
            site->regionX = regionX;
            site->regionZ = regionZ;
            return std::make_shared<const QcCoastalTowerPlan>(std::move(*site));
        }
    }
    return nullptr;
}

QcCoastalTowerPlanner::Grid QcCoastalTowerPlanner::gridOf(int regionX, int regionZ, int margin) const
{
    // The grid reaches `reach` points beyond the sites, so that the sea near a site is seen on it: a
    // site needs a point of the sea within `coastDistance` (the exact test follows in siteAt()).
    Grid grid;
    grid.step = m_settings.gridStep;
    grid.reach = m_settings.coastDistance / grid.step;
    grid.firstX = regionX * m_settings.regionSize + margin;
    grid.firstZ = regionZ * m_settings.regionSize + margin;
    grid.count = (m_settings.regionSize - 2 * margin - 1) / grid.step + 1;
    grid.width = grid.count + 2 * grid.reach;
    grid.land.resize(static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.width));
    grid.sea.resize(grid.land.size());
    for (int j = 0; j < grid.width; ++j) {
        for (int i = 0; i < grid.width; ++i) {
            const auto [x, z] = grid.column(i, j);
            const QcTerrainColumn column = m_terrain.column(x, z);
            const int top = column.height - 1;
            grid.sea[grid.index(i, j)] = isOceanColumn(column);
            grid.land[grid.index(i, j)] = top >= m_terrain.seaLevel() && top <= m_terrain.seaLevel() + m_settings.maxRise && coastTop(x, z);
        }
    }
    return grid;
}

bool QcCoastalTowerPlanner::Grid::nearSea(int i, int j) const
{
    for (int b = j - reach; b <= j + reach; ++b) {
        for (int a = i - reach; a <= i + reach; ++a) {
            if (sea[index(a, b)]) {
                return true;
            }
        }
    }
    return false;
}

QList<std::pair<int, int>> QcCoastalTowerPlanner::candidates(int regionX, int regionZ, int margin) const
{
    const Grid grid = gridOf(regionX, regionZ, margin);
    QList<std::pair<double, std::pair<int, int>>> keyed;
    for (int j = grid.reach; j < grid.reach + grid.count; ++j) {
        for (int i = grid.reach; i < grid.reach + grid.count; ++i) {
            if (grid.land[grid.index(i, j)] && grid.nearSea(i, j)) {
                const auto [x, z] = grid.column(i, j);
                keyed.append({qcPositionRandom(qcMixSeed(m_seed, Order), x, 0, z), {x, z}});
            }
        }
    }
    std::ranges::sort(keyed);

    QList<std::pair<int, int>> sites;
    sites.reserve(keyed.size());
    for (const auto &entry : std::as_const(keyed)) {
        sites.append(entry.second);
    }
    return sites;
}

std::optional<int> QcCoastalTowerPlanner::coastTop(int x, int z) const
{
    const QcTerrainGenerator::Column column = m_terrain.column(x, z);
    const int top = column.height - 1;
    const int sea = m_terrain.seaLevel();
    // No tower on the low islands of a river delta (decision E-850).
    if (column.overhang >= 1.0 || top < sea || top > sea + m_settings.maxRise || m_terrain.isDeltaBiome(column.biome)
        || m_terrain.isCave(x, top, z, column)) {
        return std::nullopt;
    }
    return top;
}

bool QcCoastalTowerPlanner::isOcean(int x, int z) const
{
    return isOceanColumn(m_terrain.column(x, z));
}

bool QcCoastalTowerPlanner::isOceanColumn(const QcTerrainColumn &column) const
{
    // The water's surface lies one below sea level.
    return column.landFactor < 0.5 && column.height - 1 <= m_terrain.seaLevel() - 1 - m_settings.oceanDepth;
}

std::optional<QcCoastalTowerPlan> QcCoastalTowerPlanner::siteAt(const Kind &kind, int x, int z, double turn) const
{
    // Each of the four turns from a random one on, the first whose door opens onto ground one can
    // step from; the footprint of a square structure is the same for all of them.
    const int first = std::min(3, static_cast<int>(turn * 4));
    // The floor per footprint: even and odd turns.
    std::array<std::optional<std::optional<int>>, 2> floors;
    for (int i = 0; i < 4; ++i) {
        const int turns = (first + i) % 4;
        const int sizeX = turns % 2 == 0 ? kind.structure->sizeX() : kind.structure->sizeZ();
        const int sizeZ = turns % 2 == 0 ? kind.structure->sizeZ() : kind.structure->sizeX();
        const Footprint footprint{x - sizeX / 2, z - sizeZ / 2, x - sizeX / 2 + sizeX - 1, z - sizeZ / 2 + sizeZ - 1};
        std::optional<std::optional<int>> &known = floors[static_cast<std::size_t>(turns % 2)];
        if (!known) {
            known = floorOver(footprint);
        }
        const std::optional<int> floor = *known;
        if (!floor) {
            continue;
        }
        QcStructurePlacement placement(kind.structure, {footprint.minX, *floor, footprint.minZ}, turns, m_content);
        const std::optional<QcBlockPos> doorstep = doorstepOf(placement);
        if (!doorstep) {
            continue;
        }
        // The sea and the neighbours are judged once per site, on the footprint of the first turn
        // whose door fits.
        const std::optional<QcBlockPos> ocean = oceanNear(footprint);
        if (!ocean || !clearOfOthers(footprint)) {
            return std::nullopt;
        }
        return QcCoastalTowerPlan{0, 0, std::move(placement), *ocean, *doorstep};
    }
    return std::nullopt;
}

std::optional<int> QcCoastalTowerPlanner::groundUnder(int x, int z) const
{
    const std::optional<int> top = coastTop(x, z);
    if (!top) {
        return std::nullopt;
    }
    for (int y = *top - 1; y > *top - solidLayers; --y) {
        if (m_terrain.isCave(x, y, z)) {
            return std::nullopt;
        }
    }
    return top;
}

std::optional<int> QcCoastalTowerPlanner::floorOver(Footprint footprint) const
{
    int low = QcWorld::worldHeight;
    int high = 0;
    for (int z = footprint.minZ; z <= footprint.maxZ; ++z) {
        for (int x = footprint.minX; x <= footprint.maxX; ++x) {
            const std::optional<int> top = groundUnder(x, z);
            if (!top) {
                return std::nullopt;
            }
            low = std::min(low, *top);
            high = std::max(high, *top);
        }
    }
    if (high - low > m_settings.maxSlope) {
        return std::nullopt;
    }
    return high;
}

std::optional<QcBlockPos> QcCoastalTowerPlanner::doorstepOf(const QcStructurePlacement &placement) const
{
    // One column outside the middle of the structure's south side.
    const QcStructure &structure = placement.structure();
    const QcBlockPos front = placement.toWorld(QcBlockPos{structure.sizeX() / 2, 0, structure.sizeZ()});
    const std::optional<int> top = coastTop(front.x, front.z);
    const int floor = placement.origin().y;
    if (!top || *top < floor - 1 || *top > floor + 1) {
        return std::nullopt;
    }
    return QcBlockPos{front.x, *top, front.z};
}

std::optional<QcBlockPos> QcCoastalTowerPlanner::oceanNear(Footprint footprint) const
{
    // Rings two blocks apart: the open sea is wide.
    for (int distance = 2; distance <= m_settings.coastDistance; distance += 2) {
        if (const std::optional<QcBlockPos> ocean = oceanOnRing(footprint.grown(distance))) {
            return ocean;
        }
    }
    return std::nullopt;
}

std::optional<QcBlockPos> QcCoastalTowerPlanner::oceanOnRing(Footprint ring) const
{
    // Every second column of the ring's edges.
    QList<std::pair<int, int>> columns;
    for (int x = ring.minX; x <= ring.maxX; x += 2) {
        columns.append({x, ring.minZ});
        columns.append({x, ring.maxZ});
    }
    for (int z = ring.minZ + 2; z < ring.maxZ; z += 2) {
        columns.append({ring.minX, z});
        columns.append({ring.maxX, z});
    }
    for (const auto &[x, z] : std::as_const(columns)) {
        if (isOcean(x, z)) {
            return QcBlockPos{x, m_terrain.seaLevel() - 1, z};
        }
    }
    return std::nullopt;
}

bool QcCoastalTowerPlanner::clearOfOthers(Footprint footprint) const
{
    const Footprint around = footprint.grown(m_settings.clearance);
    return !nearVillage(around) && !nearEntrance(around) && !nearSurfaceStructure(around);
}

bool QcCoastalTowerPlanner::nearSurfaceStructure(Footprint around) const
{
    // Surface structures are planned without the towers, so the towers keep clear of them.
    const QList<QcChunkPos> chunks = around.chunks();
    for (const QcChunkPos &chunk : chunks) {
        const QList<std::shared_ptr<const QcStructurePlacement>> plans = m_terrain.surfaceStructures().plansTouching(chunk);
        const bool near = std::ranges::any_of(plans, [&around](const std::shared_ptr<const QcStructurePlacement> &plan) {
            return around.touches(QcCoastalTowerPlan::footprintOf(*plan));
        });
        if (near) {
            return true;
        }
    }
    return false;
}

bool QcCoastalTowerPlanner::nearVillage(Footprint around) const
{
    // A village stays inside the chunks of its region.
    QSet<const QcVillagePlan *> seen;
    const QList<QcChunkPos> chunks = around.chunks();
    for (const QcChunkPos &chunk : chunks) {
        const std::shared_ptr<const QcVillagePlan> village = m_terrain.villageFor(chunk);
        if (!village || seen.contains(village.get())) {
            continue;
        }
        seen.insert(village.get());
        if (around.touches({village->minX, village->minZ, village->maxX, village->maxZ})) {
            return true;
        }
    }
    return false;
}

bool QcCoastalTowerPlanner::nearEntrance(Footprint around) const
{
    // A mine shaft entrance's headframe reaches two columns from its shaft.
    const Footprint headframes = around.grown(2);
    const QList<QcChunkPos> chunks = headframes.chunks();
    for (const QcChunkPos &chunk : chunks) {
        const QList<std::shared_ptr<const QcMineshaftPlan>> shafts = m_terrain.mineshafts().plansTouching(chunk);
        const bool near = std::ranges::any_of(shafts, [&headframes](const std::shared_ptr<const QcMineshaftPlan> &shaft) {
            return shaft->entrance && headframes.contains(shaft->entrance->top.x, shaft->entrance->top.z);
        });
        if (near) {
            return true;
        }
    }
    return false;
}
