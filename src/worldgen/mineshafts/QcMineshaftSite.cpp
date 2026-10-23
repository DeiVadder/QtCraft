#include "worldgen/mineshafts/QcMineshaftSite.h"

#include "worldgen/dungeons/QcDungeonGenerator.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <cmath>

namespace {

int floorDiv(int value, int divisor)
{
    return static_cast<int>(std::floor(static_cast<double>(value) / divisor));
}

} // namespace

QcMineshaftSite::QcMineshaftSite(const QcTerrainGenerator &terrain, const QcMineshaftSettings &settings, int regionX, int regionZ)
    : m_terrain(terrain)
    , m_settings(settings)
{
    // The dungeons whose cells may lie in the region (QcDungeonGenerator::plansTouching by columns).
    const QcDungeonGenerator &dungeons = terrain.dungeons();
    if (!dungeons.planner().enabled()) {
        return;
    }
    const int size = dungeons.planner().settings().regionSize;
    const int reach = dungeons.planner().reach();
    const int x0 = regionX * settings.regionSize;
    const int z0 = regionZ * settings.regionSize;
    const int x1 = x0 + settings.regionSize - 1;
    const int z1 = z0 + settings.regionSize - 1;
    for (int rz = floorDiv(z0 - reach, size); rz <= floorDiv(z1 + reach, size); ++rz) {
        for (int rx = floorDiv(x0 - reach, size); rx <= floorDiv(x1 + reach, size); ++rx) {
            const std::shared_ptr<const QcDungeonPlan> plan = dungeons.planOf(rx, rz);
            if (plan && plan->touches(x0, z0, x1, z1)) {
                m_dungeons.append(Box{{plan->minX, plan->origin.y - 1, plan->minZ}, {plan->maxX, plan->origin.y + plan->sizeY, plan->maxZ}});
            }
        }
    }
}

const QcTerrainColumn &QcMineshaftSite::column(int x, int z)
{
    const qint64 key = (static_cast<qint64>(x) << 32) ^ static_cast<qint64>(static_cast<quint32>(z));
    auto it = m_columns.find(key);
    if (it == m_columns.end()) {
        it = m_columns.insert(key, m_terrain.column(x, z));
    }
    return *it;
}

bool QcMineshaftSite::cave(QcBlockPos pos)
{
    return m_terrain.isCave(pos.x, pos.y, pos.z, column(pos.x, pos.z));
}

bool QcMineshaftSite::covered(int x, int z, int top)
{
    return top + m_settings.groundCover <= column(x, z).height;
}

bool QcMineshaftSite::pillarReachesGround(QcBlockPos floor)
{
    for (int depth = 1; depth <= m_settings.maxPillar; ++depth) {
        const QcBlockPos below = floor.offset(0, -depth, 0);
        if (below.y < 1) {
            return false;
        }
        if (!cave(below)) {
            return true;
        }
    }
    return false;
}

bool QcMineshaftSite::clearOfDungeons(QcBlockPos low, QcBlockPos high) const
{
    // One cell more around: liquid beside the corridors is sealed there.
    return std::ranges::none_of(m_dungeons, [&](const Box &box) {
        return high.x + 1 >= box.low.x && low.x - 1 <= box.high.x && high.y + 1 >= box.low.y && low.y - 1 <= box.high.y
               && high.z + 1 >= box.low.z && low.z - 1 <= box.high.z;
    });
}

QcMineshaftSite::Verdict QcMineshaftSite::node(const QcMineshaftPlan::Node &node)
{
    const QcBlockPos c = node.centre;
    const int h = node.half;
    const int top = c.y + node.height;
    for (const auto &[dx, dz] : {std::pair{0, 0}, {-h, -h}, {h, -h}, {-h, h}, {h, h}}) {
        if (!covered(c.x + dx, c.z + dz, top)) {
            return {};
        }
    }
    if (!clearOfDungeons(c.offset(-h, -1, -h), c.offset(h, node.height, h))) {
        return {};
    }

    Verdict verdict{true, false};
    for (int dz = -h; dz <= h && !verdict.bridge; ++dz) {
        for (int dx = -h; dx <= h && !verdict.bridge; ++dx) {
            verdict.bridge = cave(c.offset(dx, -1, dz));
        }
    }
    // The pillar under the middle carries the planks; where the middle stands on ground, so do they.
    if (verdict.bridge && cave(c.offset(0, -1, 0)) && !pillarReachesGround(c.offset(0, -1, 0))) {
        return {};
    }
    return verdict;
}

bool QcMineshaftSite::roomFits(const QcMineshaftPlan::Node &node, int half, int height)
{
    const QcBlockPos c = node.centre;
    const int top = c.y + height;
    for (const auto &[dx, dz] : {std::pair{0, 0}, {-half, -half}, {half, -half}, {-half, half}, {half, half}}) {
        if (!covered(c.x + dx, c.z + dz, top)) {
            return false;
        }
    }
    if (!clearOfDungeons(c.offset(-half, -1, -half), c.offset(half, height, half))) {
        return false;
    }
    for (int dz = -half; dz <= half; ++dz) {
        for (int dx = -half; dx <= half; ++dx) {
            if (cave(c.offset(dx, -1, dz))) {
                return false;
            }
        }
    }
    return true;
}

QcMineshaftSite::Verdict QcMineshaftSite::corridor(const QcMineshaftPlan::Corridor &corridor)
{
    const QcBlockPos first = corridor.cell(0);
    const QcBlockPos last = corridor.cell(corridor.length - 1);
    const QcBlockPos low{std::min(first.x, last.x) - 1, std::min(first.y, last.y) - 1, std::min(first.z, last.z) - 1};
    const QcBlockPos high{std::max(first.x, last.x) + 1, std::max(first.y, last.y) + corridor.height(), std::max(first.z, last.z) + 1};
    if (!clearOfDungeons(low, high)) {
        return {};
    }

    Verdict verdict{true, false};
    for (int along = 0; along < corridor.length; ++along) {
        for (int lateral = -1; lateral <= 1; ++lateral) {
            const QcBlockPos lane = corridor.cell(along, lateral);
            if (!covered(lane.x, lane.z, lane.y + corridor.height())) {
                return {};
            }
            if (cave(lane.offset(0, -1, 0))) {
                verdict.bridge = true;
            }
        }
    }
    // Staircases stand on the ground; bridges need their pillars.
    if (corridor.stairs()) {
        return verdict.bridge ? Verdict{} : verdict;
    }
    for (const int along : corridor.supports) {
        for (const int lateral : {-1, 1}) {
            const QcBlockPos floor = corridor.cell(along, lateral).offset(0, -1, 0);
            if (cave(floor) && !pillarReachesGround(floor)) {
                return {};
            }
        }
    }
    return verdict;
}
