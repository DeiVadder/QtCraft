#include "worldgen/terrain/QcCaveCarver.h"

#include "worldgen/terrain/QcTerrainColumnGrid.h"
#include "worldgen/terrain/QcTerrainDefinitions.h"
#include "worldgen/terrain/QcTerrainMath.h"

#include <algorithm>
#include <array>
#include <cmath>

using QcTerrainMath::smoothstep;

namespace {

constexpr double tunnelHorizontalScale = 56.0;
constexpr double tunnelVerticalScale = 36.0;
constexpr double cavernHorizontalScale = 90.0;
constexpr double cavernVerticalScale = 50.0;
// Tunnel width changes slowly along a tunnel: thin noodles to wide passages.
constexpr double tunnelWidthHorizontalScale = 96.0;
constexpr double tunnelWidthVerticalScale = 48.0;
// Size of the patches where caves may open to the surface.
constexpr double entranceScale = 80.0;
// Caverns stay deep underground.
constexpr int cavernMaxY = 50;
// Cave noise is evaluated on a lattice with this spacing and interpolated in between.
constexpr int caveLattice = 4;
constexpr int latticeColumns = QcWorld::chunkSize / caveLattice + 1;
constexpr int latticeRows = QcWorld::worldHeight / caveLattice + 1;

enum CaveField { TunnelA, TunnelB, Cavern, TunnelWidth, CaveFieldCount };
static_assert(CaveFieldCount == 4, "carves() takes four cave fields");

double trilinear(const std::array<double, 8> &corners, double fx, double fy, double fz)
{
    const double x00 = corners[0] + (corners[1] - corners[0]) * fx;
    const double x10 = corners[2] + (corners[3] - corners[2]) * fx;
    const double x01 = corners[4] + (corners[5] - corners[4]) * fx;
    const double x11 = corners[6] + (corners[7] - corners[6]) * fx;
    const double y0 = x00 + (x10 - x00) * fy;
    const double y1 = x01 + (x11 - x01) * fy;
    return y0 + (y1 - y0) * fz;
}

// The entrance threshold is the quantile of the entrance noise that gives the wanted share.
double entranceThreshold(const QcNoise &noise, double share)
{
    QList<double> samples;
    constexpr int grid = 64;
    samples.reserve(qsizetype(grid) * grid);
    for (int i = 0; i < grid; ++i) {
        for (int j = 0; j < grid; ++j) {
            samples.append(noise.noise2(i * 1.37, j * 1.37));
        }
    }

    std::ranges::sort(samples);
    const auto index = qsizetype((1.0 - share) * double(samples.size() - 1));
    return share >= 1.0 ? -2.0 : samples[std::clamp<qsizetype>(index, 0, samples.size() - 1)];
}

// Highest terrain column of the chunk.
int highestColumn(const QcTerrainColumnGrid &columns, int baseX, int baseZ)
{
    int maxHeight = 0;
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            maxHeight = std::max(maxHeight, columns.at(baseX + lx, baseZ + lz).height);
        }
    }
    return maxHeight;
}

} // namespace

// `rows` points high; values ordered by field, z, x, y.
struct QcCaveCarver::Lattice
{
    int rows = 0;
    QList<double> values;

    qsizetype index(int field, int ix, int iy, int iz) const
    {
        return ((qsizetype(field) * latticeColumns + iz) * latticeColumns + ix) * rows + iy;
    }

    // The fields at a block of the chunk; the same corners and interpolation as isCave(), so both
    // agree.
    std::array<double, CaveFieldCount> at(int lx, int y, int lz) const
    {
        const int ix = lx / caveLattice;
        const int iy = y / caveLattice;
        const int iz = lz / caveLattice;
        const double fx = double(lx % caveLattice) / caveLattice;
        const double fy = double(y % caveLattice) / caveLattice;
        const double fz = double(lz % caveLattice) / caveLattice;
        std::array<double, CaveFieldCount> fields{};
        for (int field = 0; field < CaveFieldCount; ++field) {
            std::array<double, 8> corners{};
            for (int corner = 0; corner < 8; ++corner) {
                corners[std::size_t(corner)] = values[index(field, ix + (corner & 1), iy + ((corner >> 1) & 1), iz + ((corner >> 2) & 1))];
            }
            fields[std::size_t(field)] = trilinear(corners, fx, fy, fz);
        }
        return fields;
    }
};

QcCaveCarver::QcCaveCarver(quint64 seed, const QcTerrainSettings &terrain, const QcTerrainDefinitions &definitions)
    : m_enabled(terrain.caves)
    , m_seaLevel(definitions.seaLevel)
    , m_bedrock(definitions.bedrock)
    , m_tunnelWidth(terrain.caveTunnelWidth)
    , m_cavernThreshold(terrain.caveCavernThreshold)
    , m_liquid(definitions.caveLiquid)
    , m_liquidLevel(terrain.caveLiquidLevel)
    , m_roof(terrain.caveRoof)
    , m_tunnelNoiseA(qcMixSeed(seed, 5))
    , m_tunnelNoiseB(qcMixSeed(seed, 6))
    , m_cavernNoise(qcMixSeed(seed, 7))
    , m_tunnelWidthNoise(qcMixSeed(seed, 14))
    , m_entranceNoise(qcMixSeed(seed, 15))
{
    if (terrain.caveEntrances > 0.0) {
        m_entranceThreshold = entranceThreshold(m_entranceNoise, terrain.caveEntrances);
    }
}

double QcCaveCarver::density(int field, int x, int y, int z) const
{
    switch (field) {
    case TunnelA:
        return m_tunnelNoiseA.noise3(x / tunnelHorizontalScale, y / tunnelVerticalScale, z / tunnelHorizontalScale);
    case TunnelB:
        return m_tunnelNoiseB.noise3(x / tunnelHorizontalScale, y / tunnelVerticalScale, z / tunnelHorizontalScale);
    case TunnelWidth:
        return m_tunnelWidthNoise.noise3(x / tunnelWidthHorizontalScale, y / tunnelWidthVerticalScale, z / tunnelWidthHorizontalScale);
    default:
        return m_cavernNoise.fractal3(x / cavernHorizontalScale, y / cavernVerticalScale, z / cavernHorizontalScale, 2);
    }
}

bool QcCaveCarver::carves(const std::array<double, 4> &values, int y) const
{
    // Tunnels: where two noise fields are both near zero, a winding line; its width follows a
    // third, slow field from thin noodles to wide passages.
    const double width = m_tunnelWidth * (0.45 + 1.1 * smoothstep(-0.35, 0.35, values[TunnelWidth]));
    const bool tunnel = std::abs(values[TunnelA]) < width && std::abs(values[TunnelB]) < width;

    // Caverns with pillars, fading out above the bedrock and towards their upper limit.
    const double threshold = m_cavernThreshold + 0.35 * (1.0 - smoothstep(3.0, 12.0, y))
                             + 0.35 * smoothstep(cavernMaxY - 16.0, double(cavernMaxY), y);
    const bool cavern = y < cavernMaxY && values[Cavern] > threshold;
    return tunnel || cavern;
}

int QcCaveCarver::top(const QcTerrainColumn &column, int x, int z) const
{
    // Keep a solid lid under seas, lakes and beaches so water never pours into caves.
    if (column.height <= m_seaLevel + 1) {
        return column.height - 6;
    }

    const bool entrance = m_entranceNoise.noise2(x / entranceScale, z / entranceScale) > m_entranceThreshold;
    return entrance ? column.height - 1 : column.height - 1 - m_roof;
}

bool QcCaveCarver::isCave(int x, int y, int z, const QcTerrainColumn &column) const
{
    if (!m_enabled || y <= 0 || y > top(column, x, z)) {
        return false;
    }

    const int lx = qcFloorDiv(x, caveLattice) * caveLattice;
    const int ly = qcFloorDiv(y, caveLattice) * caveLattice;
    const int lz = qcFloorDiv(z, caveLattice) * caveLattice;
    const double fx = double(x - lx) / caveLattice;
    const double fy = double(y - ly) / caveLattice;
    const double fz = double(z - lz) / caveLattice;
    std::array<double, CaveFieldCount> values{};
    for (int field = 0; field < CaveFieldCount; ++field) {
        std::array<double, 8> corners{};
        for (int corner = 0; corner < 8; ++corner) {
            corners[std::size_t(corner)] = density(field, lx + (corner & 1) * caveLattice, ly + ((corner >> 1) & 1) * caveLattice,
                                                   lz + ((corner >> 2) & 1) * caveLattice);
        }
        values[std::size_t(field)] = trilinear(corners, fx, fy, fz);
    }
    return carves(values, y);
}

QcCaveCarver::Lattice QcCaveCarver::lattice(QcChunkPos pos, int rows) const
{
    const int baseX = pos.x * QcWorld::chunkSize;
    const int baseZ = pos.z * QcWorld::chunkSize;
    Lattice lattice;
    lattice.rows = rows;
    lattice.values.resize(qsizetype(CaveFieldCount) * latticeColumns * latticeColumns * rows);
    for (qsizetype i = 0; i < lattice.values.size(); ++i) {
        const int iy = int(i % rows);
        const int ix = int(i / rows % latticeColumns);
        const int iz = int(i / rows / latticeColumns % latticeColumns);
        const int field = int(i / rows / latticeColumns / latticeColumns);
        lattice.values[i] = density(field, baseX + ix * caveLattice, iy * caveLattice, baseZ + iz * caveLattice);
    }
    return lattice;
}

void QcCaveCarver::carveColumn(QcChunk &chunk, const Lattice &lattice, int localX, int localZ, int columnTop) const
{
    for (int y = 1; y <= columnTop; ++y) {
        if (carves(lattice.at(localX, y, localZ), y) && chunk.block(localX, y, localZ) != m_bedrock) {
            // Deep down the caves hold lava lakes.
            chunk.setBlock(localX, y, localZ, y <= m_liquidLevel ? m_liquid : qcAirBlockId);
        }
    }
}

void QcCaveCarver::carve(QcChunk &chunk, const QcTerrainColumnGrid &columns) const
{
    if (!m_enabled) {
        return;
    }

    const int baseX = chunk.pos().x * QcWorld::chunkSize;
    const int baseZ = chunk.pos().z * QcWorld::chunkSize;
    const Lattice values = lattice(chunk.pos(), std::min(latticeRows, highestColumn(columns, baseX, baseZ) / caveLattice + 2));
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            carveColumn(chunk, values, lx, lz, top(columns.at(baseX + lx, baseZ + lz), baseX + lx, baseZ + lz));
        }
    }
}
