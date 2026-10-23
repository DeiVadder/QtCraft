#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "worldgen/generators/QcWorldGenerator.h"
#include "worldgen/surface/QcSurfacePlanner.h"
#include "worldgen/terrain/QcCaveCarver.h"
#include "worldgen/terrain/QcColdEdges.h"
#include "worldgen/terrain/QcGiantTreePlacer.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcPoolPlacer.h"
#include "worldgen/terrain/QcRiverDelta.h"
#include "worldgen/terrain/QcRiverField.h"
#include "worldgen/terrain/QcRiverPieces.h"
#include "worldgen/terrain/QcSurfaceLakes.h"
#include "worldgen/terrain/QcTerrainClimate.h"
#include "worldgen/terrain/QcTerrainColumn.h"
#include "worldgen/terrain/QcTerrainDefinitions.h"
#include "worldgen/terrain/QcTerrainSurface.h"
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

// Data-driven terrain: continents and oceans, biomes chosen by a climate map (temperature, humidity,
// ruggedness and rarity, with bent borders), rolling hills, ridged mountain ranges, terraces and rocky
// cliffs, winding rivers down to the sea level, connected cave tunnels and caverns, ore veins,
// liquid pools sunk into cave floors and features (trees, cacti, plants in patches). All blocks
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
    const QcGiantTreePlacer &giantTrees() const { return *m_giantTrees; }
    // The footprints of surface structures, coastal towers and surface lakes within `margin` columns
    // of the chunk at `pos`: no trees grow there.
    [[nodiscard]] QList<QcSurfacePlanner::Rect> structureFootprintsNear(QcChunkPos pos, int margin) const;
    // Surface lakes (decision E-856): the lake of a chunk, none in or near a village or near a
    // surface structure or coastal tower.
    const QcSurfaceLakes &lakes() const { return *m_lakes; }
    [[nodiscard]] std::optional<QcSurfaceLakes::Site> lakeIn(QcChunkPos pos) const;
    // Whether a mine shaft's entrance lies within `featureMargin` of a footprint (world) near the chunk.
    [[nodiscard]] bool mineshaftEntranceNear(QcChunkPos pos, QcSurfacePlanner::Rect footprint) const;
    QList<QcGuardSite> guardSitesNear(QcBlockPos from, int reach) const override;
    [[nodiscard]] std::optional<QList<QcGuardSite>> plannedGuardSitesNear(QcBlockPos from, int reach) const override;
    [[nodiscard]] std::optional<QcStructurePlacement> nearestStructure(const QString &name, QcBlockPos from, int reach) const override;

    // Shape and biome of a column. Near a biome border its ground biome is left `unresolvedGround`
    // (a second climate evaluation): withGround() or QcTerrainColumnGrid look it up.
    Column column(int x, int z) const;
    // The column with its ground biome looked up (decision E-858).
    [[nodiscard]] Column withGround(int x, int z, Column column) const;
    // The column whose land biome decides the ground of an unresolved column: the jittered one of
    // QcColdEdges::groundOffset(); none for a looked-up column.
    [[nodiscard]] std::optional<std::pair<int, int>> groundSource(int x, int z, const Column &column) const;
    // The ground biome of a column when the land biome at its ground source is `land` (for a river
    // channel the river biome of that land); -1 for its own.
    [[nodiscard]] int groundBiomeFor(const Column &column, int land) const;
    // The land biome of the climate at a column.
    [[nodiscard]] int landBiomeAt(int x, int z) const;
    // The world's rarity at a column in [0, 1] (decision E-821): 0 outside the spots where rare
    // biomes may appear, and everywhere when no biome is rare.
    [[nodiscard]] double rarityAt(int x, int z) const { return climateAt(x, z).rarity; }
    // The climate point of a column (decisions E-014, E-821, E-824), for tests and tools.
    [[nodiscard]] QcTerrainClimate::Point climateAt(int x, int z) const;
    // The continent value of a column (below about -0.15 the sea; river deltas read it, decision
    // E-850).
    [[nodiscard]] double continentAt(int x, int z) const;
    // The climate at a column whose ground lies above the sea level before rivers cut it (the
    // river pieces, decision E-851); std::nullopt in the sea and lakes.
    [[nodiscard]] std::optional<QcTerrainClimate::Point> dryClimateAt(int x, int z) const;
    // The biome a climate point belongs to (the heaviest after the rarity gate), as the terrain picks
    // it; tests check the layout of the biomes in climate space with it (decision E-824).
    [[nodiscard]] int biomeForClimate(const QcTerrainClimate::Point &climate) const;
    // The river near a column (decision E-840): distance to its middle line, width and depth there,
    // for tools, tests and the river deltas; `far` away without rivers and where the ground before
    // the river lies more than 8 blocks under the sea level (the sea, deep lakes), as in
    // column(). Costs a column(); column() itself gives the distance and the half width.
    [[nodiscard]] QcRiverField::Sample riverAt(int x, int z) const;
    // Whether a biome is a river biome (the water of river channels, decision E-840).
    [[nodiscard]] bool isRiverBiome(int biome) const;
    // Whether a biome is a delta biome (the arms and islands of a river delta, decision E-850).
    [[nodiscard]] bool isDeltaBiome(int biome) const;
    // The block of the top water layer of a column under the sea level (decision E-858): with
    // freezing by temperature ice where the column is cold enough, else the biome's frozenWater;
    // with a `frozenDepth` only where the water is that shallow (decision E-850).
    [[nodiscard]] QcBlockId frozenWaterAt(int x, int z, const Column &column) const;
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

    // Biome height of land before blending: base height plus hills, ridges and terraces.
    double biomeHeight(const Biome &biome, double rolling, double ridge, double terraces) const;
    // The climate blend of a column: its biome (the heaviest) and the weighted land height and
    // overhang of all biomes.
    struct Blend
    {
        int biome = 0;
        double landHeight = 0.0;
        double overhang = 0.0;
        // The heaviest biome's weight and the next one's: near 0 deep inside a biome.
        double bestWeight = 0.0;
        double runnerUpWeight = 0.0;
    };
    [[nodiscard]] Blend blend(const QcTerrainClimate::Point &climate, double rolling, double ridge, double terraces) const;
    // A column before rivers and deltas cut it: its climate, the blend of the biomes, the
    // continent value, how much of the land's height it has (0 at sea, 1 inland) and its height.
    struct Ground
    {
        QcTerrainClimate::Point climate;
        Blend blended;
        double continent = 0.0;
        double landFactor = 0.0;
        double height = 0.0;
    };
    [[nodiscard]] Ground groundAt(int x, int z) const;
    // A column of land height `height` and biome `biome` carved by the river near it (decision
    // E-840): the new height, the river biome where the river put water, and whether it is a bank.
    struct RiverCut
    {
        double height = 0.0;
        int biome = 0;
        // The land biome the river runs through.
        int land = 0;
        bool bank = false;
        // The river the column asked (none where its ground lies too deep).
        QcRiverField::Sample river;
    };
    [[nodiscard]] RiverCut riverCut(int x, int z, const QcTerrainClimate::Point &climate, double height, int biome) const;
    // The continent value at warped coordinates (below about -0.15 the sea).
    [[nodiscard]] double continentOf(double warpedX, double warpedZ) const;
    // A column cut by `river` and in the band along the coasts (continent value `continent`)
    // reshaped by a river delta (decision E-850): its new height and biome (the delta biome where
    // the delta outweighs the land); std::nullopt outside every delta.
    struct DeltaCut
    {
        double height = 0.0;
        int biome = 0;
    };
    [[nodiscard]] std::optional<DeltaCut> deltaCut(int x, int z, double continent, const RiverCut &river) const;
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
    QcTerrainClimate m_climate;
    QcRiverField m_rivers;
    QcRiverPieces m_riverPieces;
    QcRiverDelta m_deltas;
    QcColdEdges m_coldEdges;
    // The ground blocks of the columns; after the definitions and rivers it reads.
    QcTerrainSurface m_ground;
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
    // Giant trees on a 2×2 trunk (decision E-822); reads the terrain columns.
    std::unique_ptr<QcGiantTreePlacer> m_giantTrees;
    // Lakes on the surface (decision E-856); reads the terrain columns.
    std::unique_ptr<QcSurfaceLakes> m_lakes;

    QcNoise m_continentNoise;
    QcNoise m_detailNoise;
    QcNoise m_warpNoiseX;
    QcNoise m_warpNoiseZ;
    QcNoise m_hillNoise;
    QcNoise m_ridgeNoise;
    QcNoise m_terraceNoise;
    QcNoise m_overhangNoise;
};
