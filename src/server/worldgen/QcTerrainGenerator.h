#pragma once

#include "QcContentRegistry.h"
#include "QcNoise.h"
#include "QcWorldGenerator.h"

#include <QList>
#include <QSet>

#include <array>

#include <memory>

// Data-driven terrain: continents and oceans, biomes chosen by a climate map (temperature, humidity
// and ruggedness, with bent borders), rolling hills, ridged mountain ranges, terraces and rocky
// cliffs, connected cave tunnels and caverns, ore veins, liquid pools sunk into cave floors and
// features (trees, cacti, plants in patches). All blocks
// come from the registry's biome, feature, ore and terrain definitions. Deterministic per seed and
// thread-safe: generate() only reads immutable state.
class QcTerrainGenerator : public QcWorldGenerator
{
public:
    struct Column
    {
        // Height of the terrain surface: the top solid block is at height - 1.
        int height = 0;
        int biome = 0;
        // 0 = ocean, 1 = land; values in between are coasts.
        double landFactor = 1.0;
        // Largest height difference to the four neighbouring columns; set where it is needed
        // (generate(), surfaceBlockAt()), else 0.
        int slope = 0;
        // Overhang amplitude in blocks, blended across biome borders (0: plain column).
        double overhang = 0.0;
    };

    // Unresolvable references are reported in `warnings` and replaced by sensible fallbacks; omitted
    // optional blocks (empty names) fall back silently.
    QcTerrainGenerator(std::shared_ptr<const QcContentRegistry> content, qint64 seed, QStringList *warnings = nullptr);
    ~QcTerrainGenerator() override;

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override;
    QcBlockPos spawnPoint() const override;
    QString biomeAt(int x, int z) const override { return biomeName(column(x, z).biome); }

    Column column(int x, int z) const;
    // Top block of the unmodified terrain at a column (before caves and features).
    QcBlockId surfaceBlockAt(int x, int z) const;
    QString biomeName(int index) const;
    // Highest solid block of the unmodified terrain at a column (overhangs included, caves not).
    int groundTop(int x, int z, const Column &column) const;
    // True where a cave carves the terrain (independent of the surface).
    bool isCave(int x, int y, int z) const;
    int seaLevel() const { return m_seaLevel; }

private:
    struct Feature;
    struct Biome;
    struct Ore;
    struct Pool;
    // Columns of a chunk and a margin around it, computed once per chunk.
    class ColumnGrid;

    QcBlockId resolve(const QString &name, QcBlockId fallback, QStringList *warnings) const;
    QcBlockId surfaceBlock(const Column &column, int x, int z) const;
    // The ground's own block below the surface down to the subsurface depth.
    QcBlockId subsurfaceBlock(const Column &column, QcBlockId surface) const;
    // Biome height of land before blending: base height plus hills, ridges and terraces.
    double biomeHeight(const Biome &biome, double rolling, double ridge, double terraces) const;
    double caveDensity(int field, int x, int y, int z) const;
    // Whether the cave fields (interpolated at a block) remove that block.
    bool carves(const std::array<double, 4> &values, int y) const;
    // Overhangs: solidity of the band around the plain height that the 3D noise reshapes (below
    // it everything is solid, above it nothing). Fills `solid` from the band's lowest y, which it
    // returns, and gives the band's size in `size`.
    static constexpr int maxOverhangBand = 40;
    int overhangBand(int x, int z, const Column &column, std::array<bool, maxOverhangBand> &solid, int &size) const;
    // Highest block caves may remove in a column: below a rock roof except at entrances, and below
    // a thicker lid under water.
    int caveTop(const Column &column, int x, int z) const;
    void fillColumn(QcChunk &chunk, int localX, int localZ, const Column &column) const;
    void carveCaves(QcChunk &chunk, const ColumnGrid &columns) const;
    void placeOres(QcChunk &chunk) const;
    void placePools(QcChunk &chunk) const;
    // Pools with `surface`, after the features so trees never depend on them.
    void placeSurfacePools(QcChunk &chunk, const ColumnGrid &columns) const;
    void sinkPool(QcChunk &chunk, const Pool &pool, int centreX, int floorY, int centreZ, int attempt) const;
    // Whether a block holds a liquid in place (solid, neither liquid nor replaceable).
    bool holdsLiquid(QcBlockId block) const;
    // `carved`: per column of the chunk (z * chunkSize + x), whether caves removed its top block.
    void placeFeatures(QcChunk &chunk, const ColumnGrid &columns, const QList<bool> &carved) const;
    void placeFeature(QcChunk &chunk, const Feature &feature, int x, int baseY, int z, double random) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    quint64 m_seed = 0;
    int m_seaLevel = 62;
    QcBlockId m_stone = qcAirBlockId;
    QcBlockId m_water = qcAirBlockId;
    QcBlockId m_bedrock = qcAirBlockId;
    bool m_caves = true;
    double m_tunnelWidth = 0.075;
    double m_cavernThreshold = 0.58;
    QcBlockId m_caveLiquid = qcAirBlockId;
    int m_caveLiquidLevel = 10;
    int m_caveRoof = 4;
    // Entrance noise above this value lets caves open to the surface.
    double m_entranceThreshold = 1.0;
    int m_bedrockLayers = 3;
    QList<Biome> m_biomes;
    QList<Feature> m_features;
    QList<Ore> m_ores;
    QList<Pool> m_pools;
    // Whether any biome uses ridges or terraces (their noise is skipped otherwise).
    bool m_anyRidges = false;
    bool m_anyTerraces = false;
    bool m_anyOverhangs = false;

    QcNoise m_continentNoise;
    QcNoise m_detailNoise;
    QcNoise m_temperatureNoise;
    QcNoise m_humidityNoise;
    QcNoise m_tunnelNoiseA;
    QcNoise m_tunnelNoiseB;
    QcNoise m_cavernNoise;
    QcNoise m_ruggednessNoise;
    QcNoise m_warpNoiseX;
    QcNoise m_warpNoiseZ;
    QcNoise m_hillNoise;
    QcNoise m_ridgeNoise;
    QcNoise m_terraceNoise;
    QcNoise m_tunnelWidthNoise;
    QcNoise m_entranceNoise;
    QcNoise m_overhangNoise;
};
