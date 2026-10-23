#include "worldgen/terrain/QcVegetationPatchPlacer.h"

#include "common/core/QcIntMath.h"
#include "common/world/QcChunk.h"
#include "worldgen/terrain/QcTerrainColumnGrid.h"
#include "worldgen/terrain/QcTerrainGenerator.h"


using namespace Qt::StringLiterals;

namespace {

constexpr int cell = QcVegetationPatches::patchCell;
static_assert(QcVegetationPatches::maxRadius < cell, "a patch reaches only into the neighbouring cells");

// The patch cells along one axis whose patches may reach into the chunk at `chunk` on that axis.
struct CellRange
{
    int first = 0;
    int last = 0;
};

[[nodiscard]] CellRange cellsAround(int chunk)
{
    return {qcFloorDiv(chunk * QcWorld::chunkSize - QcVegetationPatches::maxRadius, cell),
            qcFloorDiv((chunk + 1) * QcWorld::chunkSize + QcVegetationPatches::maxRadius - 1, cell)};
}

} // namespace

QcVegetationPatchPlacer::QcVegetationPatchPlacer(const QcTerrainGenerator &generator, const QcTerrainDefinitions &definitions,
                                                 quint64 seed)
    : m_generator(generator)
    , m_definitions(definitions)
{
    m_seeds[QcTerrainDefinitions::Grass] = qcMixSeed(seed, qcNameSalt(u"#grass patches"_s));
    m_seeds[QcTerrainDefinitions::Flowers] = qcMixSeed(seed, qcNameSalt(u"#flowers patches"_s));
}

std::optional<QcVegetationPatchPlacer::Patch> QcVegetationPatchPlacer::patchIn(int cellX, int cellZ, int layer) const
{
    const quint64 seed = m_seeds[static_cast<std::size_t>(layer)];
    const double roll = qcPositionRandom(seed, cellX, 0, cellZ);
    // Most cells have no patch: decided before the climate of the centre is computed.
    if (roll >= m_definitions.maxPatchChance[static_cast<std::size_t>(layer)]) {
        return std::nullopt;
    }

    Patch patch;
    patch.layer = layer;
    patch.centreX = cellX * cell + static_cast<int>(qcPositionRandom(seed, cellX, 1, cellZ) * cell);
    patch.centreZ = cellZ * cell + static_cast<int>(qcPositionRandom(seed, cellX, 2, cellZ) * cell);
    patch.biome = m_generator.column(patch.centreX, patch.centreZ).biome;
    const QcTerrainDefinitions::VegetationPatches &layerPatches = m_definitions.biomes[patch.biome].patches[static_cast<std::size_t>(layer)];
    if (layerPatches.plants.isEmpty() || roll >= layerPatches.patches.chance) {
        return std::nullopt;
    }

    patch.radius = layerPatches.patches.radius;
    patch.block = layerPatches.plants.pick(qcPositionRandom(seed, cellX, 3, cellZ)).block;
    const quint64 patchSeed = qcMixSeed(seed, static_cast<quint64>(static_cast<quint32>(cellX)) << 32 | static_cast<quint32>(cellZ));
    const int span = patch.radius + 1;
    for (int attempt = 0; attempt < layerPatches.patches.tries; ++attempt) {
        // The difference of two even draws: most tries near the centre, as in the reference.
        const auto offset = [&](int axis) {
            return attempt == 0 ? 0
                                : static_cast<int>(qcPositionRandom(patchSeed, attempt, 2 * axis, 0) * span)
                                      - static_cast<int>(qcPositionRandom(patchSeed, attempt, 2 * axis + 1, 0) * span);
        };
        patch.tries.append({patch.centreX + offset(0), patch.centreZ + offset(1)});
    }
    return patch;
}

QList<QcVegetationPatchPlacer::Patch> QcVegetationPatchPlacer::patchesNear(QcChunkPos pos) const
{
    QList<Patch> patches;
    const CellRange xs = cellsAround(pos.x);
    const CellRange zs = cellsAround(pos.z);
    for (int layer = 0; layer < QcTerrainDefinitions::VegetationLayers; ++layer) {
        if (m_definitions.maxPatchChance[static_cast<std::size_t>(layer)] <= 0.0) {
            continue;
        }
        // The cells row by row, as one index.
        const int width = xs.last - xs.first + 1;
        const int cells = width * (zs.last - zs.first + 1);
        for (int index = 0; index < cells; ++index) {
            if (std::optional<Patch> patch = patchIn(xs.first + index % width, zs.first + index / width, layer)) {
                patches.append(std::move(*patch));
            }
        }
    }
    return patches;
}

void QcVegetationPatchPlacer::place(QcChunk &chunk, const QcTerrainColumnGrid &columns, const QList<bool> &carved,
                                    const ColumnFilter &allowed) const
{
    const int baseX = chunk.pos().x * QcWorld::chunkSize;
    const int baseZ = chunk.pos().z * QcWorld::chunkSize;
    const QList<Patch> patches = patchesNear(chunk.pos());
    for (const Patch &patch : patches) {
        const QSet<QcBlockId> *ground = nullptr;
        for (const QcVegetationTable::Plant &plant : m_definitions.biomes[patch.biome].patches[static_cast<std::size_t>(patch.layer)].plants.plants()) {
            ground = plant.block == patch.block ? &plant.ground : ground;
        }

        for (const auto &[x, z] : patch.tries) {
            const int lx = x - baseX;
            const int lz = z - baseZ;
            if (lx < 0 || lx >= QcWorld::chunkSize || lz < 0 || lz >= QcWorld::chunkSize || carved[lz * QcWorld::chunkSize + lx]) {
                continue;
            }
            const QcTerrainColumn &column = columns.at(x, z);
            // Dry land of the patch's own biome only, never on a shore.
            if (column.biome != patch.biome || column.height - 1 <= m_generator.seaLevel() || !allowed(x, z)) {
                continue;
            }

            const int top = m_generator.groundTop(x, z, column);
            if (top + 1 >= QcWorld::worldHeight || chunk.block(lx, top + 1, lz) != qcAirBlockId
                || (ground && !ground->isEmpty() && !ground->contains(chunk.block(lx, top, lz)))) {
                continue;
            }
            chunk.setBlock(lx, top + 1, lz, patch.block);
        }
    }
}
