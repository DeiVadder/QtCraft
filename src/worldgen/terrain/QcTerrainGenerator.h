#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "worldgen/generators/QcWorldGenerator.h"
#include "worldgen/surface/QcSurfacePlanner.h"
#include "worldgen/terrain/QcCaveCarver.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcPoolPlacer.h"
#include "worldgen/terrain/QcTerrainColumn.h"
#include "worldgen/terrain/QcTerrainDefinitions.h"
#include "worldgen/terrain/QcVegetationPatchPlacer.h"

#include <QList>
#include <QVarLengthArray>

#include <array>
#include <memory>
#include <optional>

class QcCoastalTowerGenerator;
class QcDungeonGenerator;
class QcMineshaftGenerator;
class QcSurfaceStructures;
class QcTerrainColumnGrid;
class QcVillageGenerator;

// Data-driven terrain: continents and oceans, biomes chosen by a climate map (temperature, humidity
// and ruggedness, with bent borders), rolling hills, ridged mountain ranges, terraces and rocky
// cliffs, connected cave tunnels and caverns, ore veins, liquid pools sunk into cave floors and
// features (trees, cacti, plants in patches). All blocks
// come from the registry's biome, feature, ore and terrain definitions. Deterministic per seed and
// thread-safe: generate() only reads immutable state.
class QcTerrainGenerator : public QcWorldGenerator
{
public:
    using Column = QcTerrainColumn;

    // Unresolvable references are replaced by sensible fallbacks with a warning (warnings()); omitted
    // optional blocks (empty names) fall back silently.
    QcTerrainGenerator(std::shared_ptr<const QcContentRegistry> content, qint64 seed);
    ~QcTerrainGenerator() override;

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override;
    QcBlockPos spawnPoint() const override;
    QString biomeAt(int x, int z) const override { return biomeName(column(x, z).biome); }
    bool isNaturalGround(QcBlockId block) const override { return m_definitions.naturalGround.contains(block); }
    [[nodiscard]] std::shared_ptr<const QcVillagePlan> villageFor(QcChunkPos pos) const override;
    [[nodiscard]] std::shared_ptr<const QcVillagePlan> nearestVillage(QcBlockPos from, int reach) const override;
    const QcVillageGenerator &villages() const { return *m_villages; }
    // Dungeons (decision E-522) and mine shafts (decision E-570): their loot, spawner kinds and
    // carts, and the plans for tools and tests.
    QcChunkFurnishing furnishingIn(QcChunkPos pos) const override;
    const QcDungeonGenerator &dungeons() const { return *m_dungeons; }
    const QcMineshaftGenerator &mineshafts() const { return *m_mineshafts; }
    // Structures it places by itself (decision E-590): the desert temple.
    const QcSurfaceStructures &surfaceStructures() const { return *m_surface; }
    // Coastal towers (decision E-605): their chests' loot, and the plans for tools and tests.
    const QcCoastalTowerGenerator &coastalTowers() const { return *m_coastalTowers; }
    // Patches of one kind of grass or flower (decision E-814), for tests.
    const QcVegetationPatchPlacer &vegetationPatches() const { return *m_vegetationPatches; }
    QList<QcGuardSite> guardSitesNear(QcBlockPos from, int reach) const override;
    [[nodiscard]] std::optional<QList<QcGuardSite>> plannedGuardSitesNear(QcBlockPos from, int reach) const override;
    [[nodiscard]] std::optional<QcStructurePlacement> nearestStructure(const QString &name, QcBlockPos from, int reach) const override;

    Column column(int x, int z) const;
    // Top block of the unmodified terrain at a column (before caves and features).
    QcBlockId surfaceBlockAt(int x, int z) const;
    QString biomeName(int index) const;
    // Highest solid block of the unmodified terrain at a column (overhangs included, caves not).
    int groundTop(int x, int z, const Column &column) const;
    // True where a cave carves the terrain (independent of the surface).
    bool isCave(int x, int y, int z) const;
    // The same with the terrain column at x, z known already.
    bool isCave(int x, int y, int z, const Column &column) const;
    int seaLevel() const { return m_definitions.seaLevel; }
    // The block the sea and lakes are filled with.
    QcBlockId waterBlock() const { return m_definitions.water; }

private:
    using Biome = QcTerrainDefinitions::Biome;
    using Feature = QcTerrainDefinitions::Feature;
    using Ore = QcTerrainDefinitions::Ore;

    QcBlockId surfaceBlock(const Column &column, int x, int z) const;
    // The ground's own block below the surface down to the subsurface depth.
    QcBlockId subsurfaceBlock(const Column &column, QcBlockId surface) const;
    // Biome height of land before blending: base height plus hills, ridges and terraces.
    double biomeHeight(const Biome &biome, double rolling, double ridge, double terraces) const;
    // Overhangs: solidity of the band around the plain height that the 3D noise reshapes (below
    // it everything is solid, above it nothing). `solid` starts at the band's lowest y `bottom`.
    static constexpr int maxOverhangBand = 40;
    struct OverhangBand
    {
        int bottom = 0;
        int size = 0;
        std::array<bool, maxOverhangBand> solid{};
    };
    [[nodiscard]] OverhangBand overhangBand(int x, int z, const Column &column) const;
    void fillColumn(QcChunk &chunk, int localX, int localZ, const Column &column) const;
    // Per column of the chunk (z * chunkSize + x), whether caves removed its top block.
    QList<bool> carvedColumns(const QcChunk &chunk, const QcTerrainColumnGrid &columns) const;
    void placeOres(QcChunk &chunk, const QcTerrainColumnGrid &columns) const;
    // The cells of one vein, chunk-local; on the stack up to the largest vein the content allows
    // (`veinSize` at most 64, QcWorldgenParser).
    using Vein = QVarLengthArray<QcBlockPos, 64>;
    // One compact vein of `ore`, grown from its first cell.
    [[nodiscard]] Vein grownVein(const Ore &ore, int vein, QcChunkPos pos) const;
    void placeVein(QcChunk &chunk, const Ore &ore, const Vein &cells) const;
    // `carved`: per column of the chunk (z * chunkSize + x), whether caves removed its top block.
    // Columns under and around `village` (may be null) and the footprints of surface structures
    // (`structures`) get none.
    void placeFeatures(QcChunk &chunk, const QcTerrainColumnGrid &columns, const QList<bool> &carved, const QcVillagePlan *village,
                       const QList<QcSurfacePlanner::Rect> &structures) const;
    void placeFeatureInColumn(QcChunk &chunk, const QcTerrainColumnGrid &columns, const QList<bool> &carved, int x, int z) const;
    // Plants on the water of a column under the sea level (lily pads, decision E-774).
    void placeFeatureOnWater(QcChunk &chunk, const Column &column, const QList<bool> &carved, int x, int z) const;
    // Where a column offers room for a feature: dry land, ground at sea level beside water (only
    // `nearWater` features) or the surface of water (only `onWater` features).
    enum class Spot : quint8 { Land, Shore, Water };
    // The first feature of the column's biome that grows there by density, patches and spot.
    [[nodiscard]] std::optional<int> chosenFeature(const Column &column, int x, int z, Spot spot) const;
    void placeFeature(QcChunk &chunk, const Feature &feature, QcBlockPos base, double random) const;
    // The search for the spawn point (defined in the source), which keeps the last chunk it generated.
    class SpawnSearch;

    std::shared_ptr<const QcContentRegistry> m_content;
    quint64 m_seed = 0;
    QcTerrainDefinitions m_definitions;
    QcCaveCarver m_caves;
    QcPoolPlacer m_pools;
    // Created in the constructor's body: it reads the terrain columns.
    std::unique_ptr<QcVillageGenerator> m_villages;
    std::unique_ptr<QcDungeonGenerator> m_dungeons;
    // Created after the dungeons: its plans keep clear of them.
    std::unique_ptr<QcMineshaftGenerator> m_mineshafts;
    // Created after the villages: its plans keep clear of them.
    std::unique_ptr<QcSurfaceStructures> m_surface;
    // Created after the mine shafts and the surface structures: its plans keep clear of their
    // entrances and footprints and of villages (decision E-605).
    std::unique_ptr<QcCoastalTowerGenerator> m_coastalTowers;
    // Patches of grass and flowers (decision E-814); reads the terrain columns.
    std::unique_ptr<QcVegetationPatchPlacer> m_vegetationPatches;

    QcNoise m_continentNoise;
    QcNoise m_detailNoise;
    QcNoise m_temperatureNoise;
    QcNoise m_humidityNoise;
    QcNoise m_ruggednessNoise;
    QcNoise m_warpNoiseX;
    QcNoise m_warpNoiseZ;
    QcNoise m_climateWarpNoiseX;
    QcNoise m_climateWarpNoiseZ;
    QcNoise m_hillNoise;
    QcNoise m_ridgeNoise;
    QcNoise m_terraceNoise;
    QcNoise m_overhangNoise;
};
