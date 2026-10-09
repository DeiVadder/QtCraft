#pragma once

#include "QcContentRegistry.h"
#include "QcNoise.h"
#include "QcWorldGenerator.h"

#include <QList>
#include <QSet>

#include <memory>

// Data-driven terrain: continents and oceans, biomes chosen by a climate map (temperature and
// humidity), connected cave tunnels and caverns, ore veins, liquid pools sunk into cave floors and
// features (trees, cacti). All blocks
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
    QcBlockId surfaceBlockAt(int x, int z) const { return surfaceBlock(column(x, z)); }
    QString biomeName(int index) const;
    // True where a cave carves the terrain (independent of the surface).
    bool isCave(int x, int y, int z) const;
    int seaLevel() const { return m_seaLevel; }

private:
    struct Feature;
    struct Biome;
    struct Ore;
    struct Pool;

    QcBlockId resolve(const QString &name, QcBlockId fallback, QStringList *warnings) const;
    QcBlockId surfaceBlock(Column column) const;
    double caveDensity(int field, int x, int y, int z) const;
    void fillColumn(QcChunk &chunk, int localX, int localZ, Column column) const;
    void carveCaves(QcChunk &chunk, const QList<Column> &columns) const;
    void placeOres(QcChunk &chunk) const;
    void placePools(QcChunk &chunk) const;
    void sinkPool(QcChunk &chunk, const Pool &pool, int centreX, int floorY, int centreZ, int attempt) const;
    // Whether a block holds a liquid in place (solid, neither liquid nor replaceable).
    bool holdsLiquid(QcBlockId block) const;
    void placeFeatures(QcChunk &chunk) const;
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
    QList<Biome> m_biomes;
    QList<Feature> m_features;
    QList<Ore> m_ores;
    QList<Pool> m_pools;

    QcNoise m_continentNoise;
    QcNoise m_detailNoise;
    QcNoise m_temperatureNoise;
    QcNoise m_humidityNoise;
    QcNoise m_tunnelNoiseA;
    QcNoise m_tunnelNoiseB;
    QcNoise m_cavernNoise;
};
