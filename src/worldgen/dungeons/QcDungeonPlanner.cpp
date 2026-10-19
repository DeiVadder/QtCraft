#include "worldgen/dungeons/QcDungeonPlanner.h"

#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <iterator>

using namespace Qt::StringLiterals;

namespace {

// The four sides in the order of the blocks' facing tables: north, east, south, west.
constexpr std::array<std::pair<int, int>, 4> sides{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
constexpr std::array<const char *, 4> sideNames{"north", "east", "south", "west"};

int sideOf(int dx, int dz)
{
    return int(std::ranges::find(sides, std::pair{dx, dz}) - sides.begin());
}

// The side a chest faces from the wall it stands against (0 north, 1 east, 2 south, 3 west), given its
// distances to the west, east, north and south rows along the walls; -1 when it stands at none.
int facingFrom(int west, int east, int north, int south)
{
    if (west == 0) {
        return 1;
    }
    if (east == 0) {
        return 3;
    }
    if (north == 0) {
        return 2;
    }
    return south == 0 ? 0 : -1;
}

// Rock left above the room and its corridors at least.
constexpr int groundCover = 6;
// Places of a room keep this far from their region's edges.
constexpr int regionMargin = 2;

} // namespace

class QcDungeonPlanner::Probe
{
public:
    explicit Probe(const QcTerrainGenerator &terrain)
        : m_terrain(terrain)
    {
    }

    const QcTerrainColumn &column(int x, int z)
    {
        const qint64 key = (qint64(x) << 32) ^ qint64(quint32(z));
        auto it = m_columns.find(key);
        if (it == m_columns.end()) {
            it = m_columns.insert(key, m_terrain.column(x, z));
        }
        return *it;
    }

    bool cave(QcBlockPos pos) { return m_terrain.isCave(pos.x, pos.y, pos.z, column(pos.x, pos.z)); }
    // Open for a body: the feet cell or the head cell is cave.
    bool open(QcBlockPos feet) { return cave(feet) || cave(feet.offset(0, 1, 0)); }
    // Deep enough below the ground for a corridor's cells.
    bool covered(QcBlockPos feet) { return feet.y + 2 + groundCover <= column(feet.x, feet.z).height; }

private:
    const QcTerrainGenerator &m_terrain;
    QHash<qint64, QcTerrainColumn> m_columns;
};

QcDungeonPlanner::QcDungeonPlanner(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed)
    : m_terrain(terrain)
    , m_settings(content.terrainSettings().dungeons)
    , m_seed(qcMixSeed(seed, 0x3801))
{
    if (!m_settings.enabled) {
        return;
    }

    bool resolved = true;
    const auto state = [&](const QString &name) {
        const std::optional<QcBlockId> id = content.stateId(name);
        if (!id) {
            m_warnings.append(u"dungeons: unknown block '%1', no dungeons"_s.arg(name));
        }
        resolved = resolved && id.has_value();
        return id.value_or(qcAirBlockId);
    };
    m_blocks.wall = state(m_settings.wall);
    m_blocks.moss = state(m_settings.moss);
    m_blocks.spawner = state(m_settings.spawner);
    for (std::size_t side = 0; side < sides.size(); ++side) {
        const QString name = QString::fromLatin1(sideNames[side]);
        m_blocks.chests[side] = state(m_settings.chest + u"[facing="_s + name + u']');
        m_blocks.lights[side] = state(m_settings.light + u"[attach="_s + name + u']');
    }
    const QcSpawnerDefinition &spawner = content.block(m_blocks.spawner).spawner;
    m_areaX = spawner.areaX / 2;
    m_areaY = spawner.areaY / 2;
    m_areaZ = spawner.areaZ / 2;
    m_enabled = resolved && !m_settings.mobs.isEmpty();
}

double QcDungeonPlanner::random(const QcDungeonPlan &plan, int purpose, int index) const
{
    return qcPositionRandom(qcMixSeed(m_seed, quint64(purpose)), plan.regionX, index, plan.regionZ);
}

namespace {

// The cells of a plan's corridors (feet and head).
QSet<QcBlockPos> corridorCells(const QcDungeonPlan &plan)
{
    QSet<QcBlockPos> inside;
    for (const QcDungeonPlan::Corridor &corridor : plan.corridors) {
        for (const QcBlockPos &feet : corridor.path) {
            inside.insert(feet);
            inside.insert(feet.offset(0, 1, 0));
        }
    }
    return inside;
}

} // namespace

std::shared_ptr<const QcDungeonPlan> QcDungeonPlanner::plan(int regionX, int regionZ) const
{
    if (!m_enabled) {
        return nullptr;
    }
    auto plan = std::make_shared<QcDungeonPlan>();
    plan->regionX = regionX;
    plan->regionZ = regionZ;
    if (random(*plan, 1) >= m_settings.chance) {
        return nullptr;
    }

    const int sizes = (m_settings.maxSize - m_settings.minSize) / 2 + 1;
    plan->sizeX = m_settings.minSize + 2 * std::min(sizes - 1, int(random(*plan, 2) * sizes));
    plan->sizeZ = m_settings.minSize + 2 * std::min(sizes - 1, int(random(*plan, 3) * sizes));
    Probe probe(m_terrain);
    const std::optional<QcBlockPos> origin = findRoom(probe, regionX, regionZ, plan->sizeX, plan->sizeZ);
    if (!origin) {
        return nullptr;
    }

    plan->origin = *origin;
    plan->spawner = plan->origin.offset(plan->sizeX / 2, 1, plan->sizeZ / 2);
    plan->areaLow = plan->spawner.offset(-m_areaX, -m_areaY, -m_areaZ);
    plan->areaHigh = plan->spawner.offset(m_areaX, m_areaY, m_areaZ);
    plan->mob = mobOf(*plan);
    plan->chests = chestsOf(*plan);
    plan->corridors = corridorsOf(probe, *plan);
    const QSet<QcBlockPos> inside = corridorCells(*plan);
    plan->lining = liningOf(*plan, inside);
    plan->lights = lightsOf(*plan, inside);
    plan->bound();
    return plan;
}

std::optional<QcBlockPos> QcDungeonPlanner::findRoom(Probe &probe, int regionX, int regionZ, int sizeX, int sizeZ) const
{
    const int region = m_settings.regionSize;
    QcDungeonPlan key;
    key.regionX = regionX;
    key.regionZ = regionZ;
    for (int attempt = 0; attempt < m_settings.attempts; ++attempt) {
        const int x = regionX * region + regionMargin + int(random(key, 10, attempt) * std::max(1, region - 2 * regionMargin - sizeX));
        const int z = regionZ * region + regionMargin + int(random(key, 11, attempt) * std::max(1, region - 2 * regionMargin - sizeZ));
        const int y = m_settings.minY + int(random(key, 12, attempt) * (m_settings.maxY - m_settings.minY + 1));
        const QcBlockPos origin{x, y, z};
        if (deepEnough(probe, origin, sizeX, sizeZ) && fewCaves(probe, origin, sizeX, sizeZ, m_settings.maxCaveShare * 5 * sizeX * sizeZ)) {
            return origin;
        }
    }
    return std::nullopt;
}

bool QcDungeonPlanner::deepEnough(Probe &probe, QcBlockPos origin, int sizeX, int sizeZ)
{
    // The corners and the middle.
    const std::array<std::pair<int, int>, 5> columns{{{0, 0}, {sizeX - 1, 0}, {0, sizeZ - 1}, {sizeX - 1, sizeZ - 1}, {sizeX / 2, sizeZ / 2}}};
    return std::ranges::all_of(columns, [&](std::pair<int, int> at) {
        return origin.y + 5 + groundCover <= probe.column(origin.x + at.first, origin.z + at.second).height;
    });
}

bool QcDungeonPlanner::fewCaves(Probe &probe, QcBlockPos origin, int sizeX, int sizeZ, double limit)
{
    int caves = 0;
    for (int cell = 0; cell < 5 * sizeX * sizeZ; ++cell) {
        caves += probe.cave(origin.offset(cell % sizeX, cell / (sizeX * sizeZ), cell / sizeX % sizeZ)) ? 1 : 0;
        if (caves > limit) {
            return false;
        }
    }
    return true;
}

QString QcDungeonPlanner::mobOf(const QcDungeonPlan &plan) const
{
    int total = 0;
    for (const QcDungeonSettings::Mob &mob : m_settings.mobs) {
        total += mob.weight;
    }
    int pick = int(random(plan, 20) * total);
    const auto chosen = std::ranges::find_if(m_settings.mobs, [&pick](const QcDungeonSettings::Mob &mob) {
        pick -= mob.weight;
        return pick < 0;
    });
    return chosen != m_settings.mobs.end() ? chosen->mob : m_settings.mobs.constLast().mob;
}

QList<QcDungeonPlan::Placed> QcDungeonPlanner::chestsOf(const QcDungeonPlan &plan) const
{
    QList<QcDungeonPlan::Placed> chests;
    QList<QcDungeonPlan::Placed> places = chestPlaces(plan);
    const int count = m_settings.minChests + int(random(plan, 21) * (m_settings.maxChests - m_settings.minChests + 1));
    for (int i = 0; i < count && !places.isEmpty(); ++i) {
        chests.append(places.takeAt(qsizetype(random(plan, 22, i) * double(places.size()))));
    }
    return chests;
}

QList<QcDungeonPlan::Placed> QcDungeonPlanner::chestPlaces(const QcDungeonPlan &plan) const
{
    // Floor cells along the walls, facing into the room, not in front of a door (the middle of each
    // wall) and not the spawner.
    QList<QcDungeonPlan::Placed> places;
    const int x0 = plan.origin.x + 1;
    const int x1 = plan.origin.x + plan.sizeX - 2;
    const int z0 = plan.origin.z + 1;
    const int z1 = plan.origin.z + plan.sizeZ - 2;
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const bool middle = std::abs(x - plan.spawner.x) <= 1 || std::abs(z - plan.spawner.z) <= 1;
            const int side = middle ? -1 : facingFrom(x - x0, x1 - x, z - z0, z1 - z);
            if (side >= 0) {
                places.append({{x, plan.spawner.y, z}, m_blocks.chests[std::size_t(side)]});
            }
        }
    }
    return places;
}

QList<QcDungeonPlan::Corridor> QcDungeonPlanner::corridorsOf(Probe &probe, const QcDungeonPlan &plan) const
{
    const int y = plan.origin.y + 1;
    const int midX = plan.origin.x + plan.sizeX / 2;
    const int midZ = plan.origin.z + plan.sizeZ / 2;
    const std::array<QcBlockPos, 4> doors{{{midX, y, plan.origin.z}, {plan.origin.x + plan.sizeX - 1, y, midZ}, {midX, y, plan.origin.z + plan.sizeZ - 1}, {plan.origin.x, y, midZ}}};
    QList<QcDungeonPlan::Corridor> found;
    for (std::size_t side = 0; side < sides.size(); ++side) {
        if (std::optional<QcDungeonPlan::Corridor> corridor = corridorFrom(probe, doors[side], sides[side].first, sides[side].second)) {
            found.append(std::move(*corridor));
        }
    }
    std::ranges::stable_sort(found, {}, [](const QcDungeonPlan::Corridor &corridor) { return corridor.path.size(); });
    found.resize(std::min<qsizetype>(found.size(), m_settings.corridors));
    return found;
}

std::optional<int> QcDungeonPlanner::firstOpen(Probe &probe, QcBlockPos from, std::pair<int, int> step, int steps)
{
    for (int s = 1; s <= steps; ++s) {
        const QcBlockPos at = from.offset(step.first * s, 0, step.second * s);
        if (!probe.covered(at)) {
            return std::nullopt;
        }
        if (probe.open(at)) {
            return s;
        }
    }
    return std::nullopt;
}

std::optional<QcDungeonPlan::Corridor> QcDungeonPlanner::corridorFrom(Probe &probe, QcBlockPos door, int dx, int dz) const
{
    const int reach = std::min(m_settings.reach, m_settings.maxLength);
    const auto walk = [](QcDungeonPlan::Corridor &corridor, std::pair<int, int> step, int steps) {
        const QcBlockPos from = corridor.path.constLast();
        for (int s = 1; s <= steps; ++s) {
            corridor.path.append(from.offset(step.first * s, 0, step.second * s));
            corridor.steps.append(step);
        }
    };

    QcDungeonPlan::Corridor corridor{{door}, {}};
    if (const std::optional<int> straight = firstOpen(probe, door, {dx, dz}, reach)) {
        walk(corridor, {dx, dz}, *straight);
        return corridor;
    }

    // With one bend after every second cell of the first half: the shortest way found.
    struct Bend
    {
        int first = 0;
        std::pair<int, int> turn;
        int second = 0;
    };
    std::optional<Bend> best;
    for (int a = 2; a <= reach / 2 && probe.covered(door.offset(dx * a, 0, dz * a)); a += 2) {
        for (const int side : {1, -1}) {
            const std::pair<int, int> turn{-dz * side, dx * side};
            const int limit = best ? std::min(reach - a, best->first + best->second - a - 1) : reach - a;
            if (const std::optional<int> s = firstOpen(probe, door.offset(dx * a, 0, dz * a), turn, limit)) {
                best = Bend{a, turn, *s};
            }
        }
    }
    if (!best) {
        return std::nullopt;
    }
    walk(corridor, {dx, dz}, best->first);
    walk(corridor, best->turn, best->second);
    return corridor;
}

QList<QcBlockPos> QcDungeonPlanner::liningOf(const QcDungeonPlan &plan, const QSet<QcBlockPos> &inside)
{
    QSet<QcBlockPos> lining;
    for (const QcDungeonPlan::Corridor &corridor : std::as_const(plan.corridors)) {
        // Not at the door (in the wall) nor in the cave at the end.
        for (qsizetype i = 1; i + 1 < corridor.path.size(); ++i) {
            const QList<QcBlockPos> cells = around(corridor, i);
            const auto outside = [&](QcBlockPos cell) { return !inside.contains(cell) && !plan.inRoom(cell); };
            std::ranges::copy_if(cells, std::inserter(lining, lining.end()), outside);
        }
    }
    QList<QcBlockPos> sorted(lining.begin(), lining.end());
    std::ranges::sort(sorted);
    return sorted;
}

QList<QcBlockPos> QcDungeonPlanner::around(const QcDungeonPlan::Corridor &corridor, qsizetype index)
{
    // Floor and ceiling, and both sides at feet and head height for the steps into and out of the cell.
    const QcBlockPos feet = corridor.path[index];
    QList<QcBlockPos> cells{feet.offset(0, -1, 0), feet.offset(0, 2, 0)};
    for (const qsizetype step : {index - 1, index}) {
        if (step >= corridor.steps.size()) {
            continue;
        }
        const auto [sx, sz] = corridor.steps[step];
        for (const int dy : {0, 1}) {
            cells.append(feet.offset(-sz, dy, sx));
            cells.append(feet.offset(sz, dy, -sx));
        }
    }
    return cells;
}

QList<QcDungeonPlan::Placed> QcDungeonPlanner::lightsOf(const QcDungeonPlan &plan, const QSet<QcBlockPos> &inside) const
{
    QList<QcDungeonPlan::Placed> lights;
    // From the cave end inwards every `lightSpacing` cells, never near the spawn area.
    for (const QcDungeonPlan::Corridor &corridor : std::as_const(plan.corridors)) {
        const qsizetype last = corridor.path.size() - 1;
        for (qsizetype i = last - 1 - m_settings.lightSpacing / 2; i >= 1; i -= m_settings.lightSpacing) {
            const QcBlockPos head = corridor.path[i].offset(0, 1, 0);
            if (plan.distanceToArea(head) < m_settings.lightDistance) {
                continue;
            }
            if (const std::optional<QcDungeonPlan::Placed> light = lightAt(plan, head, corridor.steps[i - 1], inside)) {
                lights.append(*light);
            }
        }
    }
    return lights;
}

std::optional<QcDungeonPlan::Placed> QcDungeonPlanner::lightAt(const QcDungeonPlan &plan, QcBlockPos head, std::pair<int, int> step,
                                                               const QSet<QcBlockPos> &inside) const
{
    // On the wall to the left of the way out, else on the right.
    for (const int turn : {1, -1}) {
        const int wx = -step.second * turn;
        const int wz = step.first * turn;
        const QcBlockPos wall = head.offset(wx, 0, wz);
        if (!inside.contains(wall) && !plan.inRoom(wall)) {
            return QcDungeonPlan::Placed{head, m_blocks.lights[std::size_t(sideOf(wx, wz))]};
        }
    }
    return std::nullopt;
}
