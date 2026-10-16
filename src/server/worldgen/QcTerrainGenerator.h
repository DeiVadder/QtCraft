#pragma once

#include "QcCaveCarver.h"
#include "QcContentRegistry.h"
#include "QcNoise.h"
#include "QcPoolPlacer.h"
#include "QcTerrainColumn.h"
#include "QcTerrainDefinitions.h"
#include "QcWorldGenerator.h"

#include <QList>

#include <array>
#include <memory>
#include <optional>

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

    // Unresolvable references are reported in `warnings` and replaced by sensible fallbacks; omitted
    // optional blocks (empty names) fall back silently.
    QcTerrainGenerator(std::shared_ptr<const QcContentRegistry> content, qint64 seed, QStringList *warnings = nullptr);
    ~QcTerrainGenerator() override;

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override;
    QcBlockPos spawnPoint() const override;
    QString biomeAt(int x, int z) const override { return biomeName(column(x, z).biome); }
    bool isNaturalGround(QcBlockId block) const override { return m_definitions.naturalGround.contains(block); }
    [[nodiscard]] std::shared_ptr<const QcVillagePlan> villageFor(QcChunkPos pos) const override;
    [[nodiscard]] std::shared_ptr<const QcVillagePlan> nearestVillage(QcBlockPos from, int reach) const override;
    const QcVillageGenerator &villages() const { return *m_villages; }

    Column column(int x, int z) const;
    // Top block of the unmodified terrain at a column (before caves and features).
    QcBlockId surfaceBlockAt(int x, int z) const;
    QString biomeName(int index) const;
    // Highest solid block of the unmodified terrain at a column (overhangs included, caves not).
    int groundTop(int x, int z, const Column &column) const;
    // True where a cave carves the terrain (independent of the surface).
    bool isCave(int x, int y, int z) const;
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
    // it everything is solid, above it nothing). Fills `solid` from the band's lowest y, which it
    // returns, and gives the band's size in `size`.
    static constexpr int maxOverhangBand = 40;
    int overhangBand(int x, int z, const Column &column, std::array<bool, maxOverhangBand> &solid, int &size) const;
    void fillColumn(QcChunk &chunk, int localX, int localZ, const Column &column) const;
    // Per column of the chunk (z * chunkSize + x), whether caves removed its top block.
    QList<bool> carvedColumns(const QcChunk &chunk, const QcTerrainColumnGrid &columns) const;
    void placeOres(QcChunk &chunk) const;
    // Grows one compact vein of `ore` into `cells` (chunk-local, starting from cells[0]).
    void growVein(const Ore &ore, int vein, QcChunkPos pos, QList<QcBlockPos> &cells) const;
    void placeVein(QcChunk &chunk, const Ore &ore, const QList<QcBlockPos> &cells) const;
    // `carved`: per column of the chunk (z * chunkSize + x), whether caves removed its top block.
    // Columns under and around `village` (may be null) get none.
    void placeFeatures(QcChunk &chunk, const QcTerrainColumnGrid &columns, const QList<bool> &carved, const QcVillagePlan *village) const;
    void placeFeatureInColumn(QcChunk &chunk, const QcTerrainColumnGrid &columns, const QList<bool> &carved, int x, int z) const;
    // The first feature of the column's biome that grows there by density, patches and shore.
    [[nodiscard]] std::optional<int> chosenFeature(const Column &column, int x, int z, bool shore) const;
    void placeFeature(QcChunk &chunk, const Feature &feature, QcBlockPos base, double random) const;
    // Spawn candidate on the square ring of `radius` around the origin, if any.
    [[nodiscard]] std::optional<QcBlockPos> spawnOnRing(int radius, std::shared_ptr<QcChunk> &chunk) const;
    // Open, dry and not too high ground at a column; `chunk` caches the last generated chunk.
    bool fitsSpawn(int x, int z, const Column &column, std::shared_ptr<QcChunk> &chunk) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    quint64 m_seed = 0;
    QcTerrainDefinitions m_definitions;
    QcCaveCarver m_caves;
    QcPoolPlacer m_pools;
    // Created in the constructor's body: it reads the terrain columns.
    std::unique_ptr<QcVillageGenerator> m_villages;

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
