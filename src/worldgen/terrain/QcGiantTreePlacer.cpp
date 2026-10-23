#include "worldgen/terrain/QcGiantTreePlacer.h"

#include "common/core/QcIntMath.h"
#include "common/world/QcChunk.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/terrain/QcTreeShape.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr int cell = QcTerrainDefinitions::giantCell;
static_assert(QcGiantTreePlacer::maxReach < cell, "a giant reaches only into the neighbouring cells");
// Highest difference of the ground under the four trunk blocks.
constexpr int maxStep = 1;

} // namespace

QcGiantTreePlacer::QcGiantTreePlacer(const QcTerrainGenerator &generator, const QcTerrainDefinitions &definitions, quint64 seed)
    : m_generator(generator)
    , m_definitions(definitions)
    , m_seed(qcMixSeed(seed, qcNameSalt(u"#giant trees"_s)))
    , m_shapeSeed(seed)
{
}

std::optional<int> QcGiantTreePlacer::chosen(int biome, double roll) const
{
    double sum = 0.0;
    for (const auto &[feature, chance] : m_definitions.biomes[biome].giants) {
        sum += chance;
        if (roll < sum) {
            return feature;
        }
    }
    return std::nullopt;
}

std::optional<int> QcGiantTreePlacer::baseHeight(int x, int z, const QcTerrainDefinitions::Feature &feature) const
{
    int lowest = QcWorld::worldHeight;
    int highest = 0;
    for (const auto &[dx, dz] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}}) {
        const QcTerrainColumn column = m_generator.column(x + dx, z + dz);
        if (column.height - 1 <= m_generator.seaLevel()) {
            return std::nullopt;
        }

        const int top = m_generator.groundTop(x + dx, z + dz, column);
        const bool groundOk = feature.ground.isEmpty() || feature.ground.contains(m_generator.surfaceBlockAt(x + dx, z + dz));
        if (!groundOk || m_generator.isCave(x + dx, top, z + dz, column)) {
            return std::nullopt;
        }
        lowest = std::min(lowest, top);
        highest = std::max(highest, top);
    }
    return highest - lowest <= maxStep ? std::optional(lowest + 1) : std::nullopt;
}

std::optional<QcGiantTreePlacer::Giant> QcGiantTreePlacer::giantIn(int cellX, int cellZ) const
{
    const double roll = qcPositionRandom(m_seed, cellX, 0, cellZ);
    // Most cells have no giant: decided before the climate of the spot is computed.
    if (roll >= m_definitions.maxGiantChance) {
        return std::nullopt;
    }

    // The spot leaves room for the second trunk row inside the cell.
    const int x = cellX * cell + static_cast<int>(qcPositionRandom(m_seed, cellX, 1, cellZ) * (cell - 1));
    const int z = cellZ * cell + static_cast<int>(qcPositionRandom(m_seed, cellX, 2, cellZ) * (cell - 1));
    const std::optional<int> feature = chosen(m_generator.column(x, z).biome, roll);
    if (!feature) {
        return std::nullopt;
    }

    const std::optional<int> y = baseHeight(x, z, m_definitions.features[*feature]);
    if (!y || *y + QcTreeShape::height(m_definitions.features[*feature].shape, 1.0) + 4 >= QcWorld::worldHeight) {
        return std::nullopt;
    }
    return Giant{{x, *y, z}, *feature};
}

QList<QcGiantTreePlacer::Giant> QcGiantTreePlacer::giantsNear(QcChunkPos pos) const
{
    QList<Giant> giants;
    if (m_definitions.maxGiantChance <= 0.0) {
        return giants;
    }

    // Cells whose giants (base within the cell, crown up to maxReach beyond its trunk) may reach the chunk.
    const auto first = [](int chunk) { return qcFloorDiv(chunk * QcWorld::chunkSize - maxReach - 1, cell); };
    const auto last = [](int chunk) { return qcFloorDiv((chunk + 1) * QcWorld::chunkSize + maxReach - 1, cell); };
    for (int cellZ = first(pos.z); cellZ <= last(pos.z); ++cellZ) {
        for (int cellX = first(pos.x); cellX <= last(pos.x); ++cellX) {
            if (std::optional<Giant> giant = giantIn(cellX, cellZ)) {
                giants.append(*giant);
            }
        }
    }
    return giants;
}

bool QcGiantTreePlacer::blocked(Giant giant, const QcVillagePlan *village, const QList<QcSurfacePlanner::Rect> &structures)
{
    const QcSurfacePlanner::Rect trunk{giant.base.x, giant.base.z, giant.base.x + 1, giant.base.z + 1};
    if (village && village->covers(giant.base.x, giant.base.z, maxReach + 1)) {
        return true;
    }
    return std::ranges::any_of(structures, [&](QcSurfacePlanner::Rect footprint) { return footprint.intersects(trunk, maxReach); });
}

void QcGiantTreePlacer::build(QcChunk &chunk, const QcVillagePlan *village) const
{
    const QList<Giant> giants = giantsNear(chunk.pos());
    if (giants.isEmpty()) {
        return;
    }

    // Every structure within the reach of any of these giants, so each chunk of a giant decides alike.
    const QList<QcSurfacePlanner::Rect> structures = m_generator.structureFootprintsNear(chunk.pos(), 2 * maxReach + 1);
    for (const Giant &giant : giants) {
        if (!blocked(giant, village, structures)) {
            const QcTerrainDefinitions::Feature &feature = m_definitions.features[giant.feature];
            const double random = qcPositionRandom(feature.seed, giant.base.x, 1, giant.base.z);
            QcTreeShape::buildInChunk(chunk, feature.shape, giant.base, random, m_shapeSeed);
        }
    }
}
