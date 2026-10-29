// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "headless/QcWorldStats.h"

#include "common/content/registry/QcContentRegistry.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QHash>
#include <QMap>
#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace {

// Underground bands for the ore report, as [from, to) in blocks.
constexpr std::array<std::pair<int, int>, 5> bands{{{1, 16}, {16, 32}, {32, 64}, {64, 96}, {96, 256}}};
// Grid spacing of the biome region sample in blocks.
constexpr int biomeStep = 16;
// Land columns counted for slopes and heights, around the origin.
constexpr int terrainRadius = 2048;
constexpr int oceanBiome = -1;

struct ChunkCounts
{
    // Block -> count below the subsurface, per band and in total; exposed: next to air.
    QHash<QcBlockId, qint64> underground;
    QHash<QcBlockId, qint64> exposed;
    std::array<QHash<QcBlockId, qint64>, bands.size()> perBand;
    // Biome -> top block (first non-air from above) -> count, dry land only.
    QHash<int, QHash<QcBlockId, qint64>> surface;
    // Cave air and all cells below the soil layer, per band; dry columns and those opened by caves.
    std::array<qint64, bands.size()> caveAir{};
    std::array<qint64, bands.size()> cells{};
    qint64 dryColumns = 0;
    qint64 openColumns = 0;
    // A few places worth a look: lava on the surface and ground opened by a cave.
    QList<QcBlockPos> lavaLakes;
    QList<QcBlockPos> entrances;

    // Adds the counts of another part of the world.
    void add(const ChunkCounts &part);
};

QString percent(double part, double whole, int precision = 1)
{
    return whole > 0.0 ? QString::number(100.0 * part / whole, 'f', precision) + u'%' : u"-"_s;
}

QString perMille(qint64 part, qint64 whole)
{
    return whole > 0 ? QString::number(1000.0 * static_cast<double>(part) / static_cast<double>(whole), 'f', 2) : u"-"_s;
}

QString biomeLabel(const QcTerrainGenerator &generator, int biome)
{
    return biome == oceanBiome ? u"ocean"_s : generator.biomeName(biome);
}

bool inBand(int y, std::size_t band)
{
    return y >= bands[band].first && y < bands[band].second;
}

// Whether a neighbour of the block inside the chunk is air.
bool airBeside(const QcChunk &chunk, int lx, int y, int lz)
{
    const auto air = [&](int dx, int dy, int dz) {
        const int nx = lx + dx;
        const int nz = lz + dz;
        return nx >= 0 && nx < QcWorld::chunkSize && nz >= 0 && nz < QcWorld::chunkSize && chunk.block(nx, y + dy, nz) == qcAirBlockId;
    };
    return air(1, 0, 0) || air(-1, 0, 0) || air(0, 1, 0) || air(0, -1, 0) || air(0, 0, 1) || air(0, 0, -1);
}

// Counts one generated chunk against the generator and content it comes from: the blocks of each
// column underground, and the ground and top block of its dry columns.
class ChunkCounter
{
public:
    ChunkCounter(const QcTerrainGenerator &generator, const QcContentRegistry &content, QcChunkPos pos)
        : m_generator(generator)
        , m_content(content)
        , m_pos(pos)
        , m_chunk(generator.generate(pos))
    {
    }

    ChunkCounts count()
    {
        for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
            for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
                const QcTerrainGenerator::Column column =
                    m_generator.column(m_pos.x * QcWorld::chunkSize + lx, m_pos.z * QcWorld::chunkSize + lz);
                // Below the dirt or sand layer of the deepest biome.
                countUnderground(lx, lz, column.height - 6);
                if (column.height - 1 >= m_generator.seaLevel()) {
                    countSurface(column, lx, lz);
                }
            }
        }
        return m_counts;
    }

private:
    // The blocks of a column from y 1 up to below `deepest`.
    void countUnderground(int lx, int lz, int deepest)
    {
        const QcChunk &chunk = *m_chunk;
        for (int y = 1; y < deepest; ++y) {
            const QcBlockId block = chunk.block(lx, y, lz);
            for (std::size_t band = 0; band < bands.size(); ++band) {
                if (inBand(y, band)) {
                    m_counts.cells[band] += 1;
                    m_counts.caveAir[band] += block == qcAirBlockId;
                }
            }
            if (block == qcAirBlockId) {
                continue;
            }

            m_counts.underground[block] += 1;
            for (std::size_t band = 0; band < bands.size(); ++band) {
                if (inBand(y, band)) {
                    m_counts.perBand[band][block] += 1;
                }
            }
            if (airBeside(chunk, lx, y, lz)) {
                m_counts.exposed[block] += 1;
            }
        }
    }

    // The ground and top block of a dry column.
    void countSurface(const QcTerrainGenerator::Column &column, int lx, int lz)
    {
        const QcChunk &chunk = *m_chunk;
        const int x = chunk.pos().x * QcWorld::chunkSize + lx;
        const int z = chunk.pos().z * QcWorld::chunkSize + lz;
        m_counts.dryColumns += 1;
        const int ground = m_generator.groundTop(x, z, column);
        if (chunk.block(lx, ground, lz) == qcAirBlockId) {
            m_counts.openColumns += 1;
            if (m_counts.entrances.isEmpty()) {
                m_counts.entrances.append({x, ground, z});
            }
        }

        const int top = chunk.highestNonAir(lx, lz);
        if (m_counts.lavaLakes.isEmpty() && m_content.block(chunk.block(lx, top, lz)).liquid && top >= m_generator.seaLevel()
            && m_content.block(chunk.block(lx, top, lz)).name.endsWith(u"lava"_s)) {
            m_counts.lavaLakes.append({x, top, z});
        }
        m_counts.surface[column.biome][chunk.block(lx, top, lz)] += 1;
    }

    const QcTerrainGenerator &m_generator;
    const QcContentRegistry &m_content;
    QcChunkPos m_pos;
    std::shared_ptr<QcChunk> m_chunk;
    ChunkCounts m_counts;
};

void ChunkCounts::add(const ChunkCounts &part)
{
    for (auto it = part.underground.cbegin(); it != part.underground.cend(); ++it) {
        underground[it.key()] += it.value();
    }
    for (auto it = part.exposed.cbegin(); it != part.exposed.cend(); ++it) {
        exposed[it.key()] += it.value();
    }
    for (std::size_t band = 0; band < bands.size(); ++band) {
        for (auto it = part.perBand[band].cbegin(); it != part.perBand[band].cend(); ++it) {
            perBand[band][it.key()] += it.value();
        }
    }
    for (std::size_t band = 0; band < bands.size(); ++band) {
        caveAir[band] += part.caveAir[band];
        cells[band] += part.cells[band];
    }

    dryColumns += part.dryColumns;
    openColumns += part.openColumns;
    lavaLakes.append(part.lavaLakes);
    entrances.append(part.entrances);
    for (auto biome = part.surface.cbegin(); biome != part.surface.cend(); ++biome) {
        for (auto it = biome->cbegin(); it != biome->cend(); ++it) {
            surface[biome.key()][it.key()] += it.value();
        }
    }
}

struct Region
{
    int biome = 0;
    qint64 cells = 0;
    qint64 edges = 0;
};

// Biome per grid cell (row by row, `size` cells wide); oceanBiome for water.
struct BiomeGrid
{
    int size = 0;
    QList<int> cells;
};

BiomeGrid sampleBiomes(const QcTerrainGenerator &generator, int radius)
{
    BiomeGrid grid;
    grid.size = 2 * radius / biomeStep;
    grid.cells.resize(static_cast<qsizetype>(grid.size) * grid.size);
    QtConcurrent::blockingMap(grid.cells, [&](int &cell) {
        const qsizetype index = &cell - grid.cells.data();
        const int x = -radius + static_cast<int>(index % grid.size) * biomeStep;
        const int z = -radius + static_cast<int>(index / grid.size) * biomeStep;
        const QcTerrainGenerator::Column column = generator.column(x, z);
        // Water does not split biomes: regions describe the climate map, the ocean share is extra.
        cell = column.landFactor < 0.5 ? oceanBiome : column.biome;
    });
    return grid;
}

// The region of every grid cell, -1 for cells not reached yet.
struct RegionLabels
{
    QList<int> ids;

    // The connected cells of `start`'s biome, labelled with `id`.
    Region flood(const BiomeGrid &grid, qsizetype start, int id);
};

Region RegionLabels::flood(const BiomeGrid &grid, qsizetype start, int id)
{
    Region region;
    region.biome = grid.cells[start];
    ids[start] = id;

    QList<qsizetype> stack{start};
    while (!stack.isEmpty()) {
        const qsizetype cell = stack.takeLast();
        region.cells += 1;
        const int cx = static_cast<int>(cell % grid.size);
        const int cz = static_cast<int>(cell / grid.size);

        for (const auto &[dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const int nx = cx + dx;
            const int nz = cz + dz;
            if (nx < 0 || nx >= grid.size || nz < 0 || nz >= grid.size) {
                continue;
            }

            const qsizetype neighbour = static_cast<qsizetype>(nz) * grid.size + nx;
            if (grid.cells[neighbour] != region.biome) {
                region.edges += 1;
            } else if (ids[neighbour] < 0) {
                ids[neighbour] = id;
                stack.append(neighbour);
            }
        }
    }

    return region;
}

QList<Region> biomeRegions(const QcTerrainGenerator &generator, int radius)
{
    const BiomeGrid grid = sampleBiomes(generator, radius);
    RegionLabels labels{QList<int>(grid.cells.size(), -1)};
    QList<Region> regions;
    for (qsizetype start = 0; start < grid.cells.size(); ++start) {
        if (labels.ids[start] < 0) {
            regions.append(labels.flood(grid, start, static_cast<int>(regions.size())));
        }
    }
    return regions;
}

QStringList undergroundLines(const ChunkCounts &total, const QcContentRegistry &content, qsizetype chunks)
{
    QStringList lines;
    lines << u"== Underground (%1 chunks, below the soil layer; per mille of solid blocks)"_s.arg(chunks);
    qint64 solid = 0;
    qint64 exposedSolid = 0;
    for (auto it = total.underground.cbegin(); it != total.underground.cend(); ++it) {
        solid += it.value();
        exposedSolid += total.exposed.value(it.key());
    }
    QList<QcBlockId> blocks = total.underground.keys();
    std::ranges::sort(blocks, [&](QcBlockId a, QcBlockId b) { return total.underground[a] > total.underground[b]; });

    QString header = u"%1 %2 %3 %4"_s.arg(u"block"_s, -16).arg(u"all"_s, 8).arg(u"exposed"_s, 8).arg(u"per chunk"_s, 10);
    for (const auto &[from, to] : bands) {
        header += u"%1"_s.arg(u"y%1-%2"_s.arg(from).arg(to - 1), 9);
    }
    lines << header;
    for (const QcBlockId block : std::as_const(blocks)) {
        QString line = u"%1 %2 %3 %4"_s.arg(content.block(block).name, -16)
                           .arg(perMille(total.underground[block], solid), 8)
                           .arg(perMille(total.exposed.value(block), exposedSolid), 8)
                           .arg(static_cast<double>(total.underground[block]) / static_cast<double>(chunks), 10, 'f', 1);
        for (std::size_t band = 0; band < bands.size(); ++band) {
            qint64 bandSolid = 0;
            for (const qint64 count : total.perBand[band]) {
                bandSolid += count;
            }
            line += u"%1"_s.arg(perMille(total.perBand[band].value(block), bandSolid), 9);
        }
        lines << line;
    }
    return lines;
}

// The nearest six places, closest first.
QString nearestPlaces(const QList<QcBlockPos> &found)
{
    QList<QcBlockPos> list = found;
    std::ranges::sort(list, {}, [](QcBlockPos pos) { return static_cast<qint64>(pos.x) * pos.x + static_cast<qint64>(pos.z) * pos.z; });
    QStringList parts;
    const QList<QcBlockPos> nearest = list.first(std::min<qsizetype>(list.size(), 6));
    for (const QcBlockPos &pos : nearest) {
        parts << u"%1,%2,%3"_s.arg(pos.x).arg(pos.y).arg(pos.z);
    }
    return parts.join(u"  "_s);
}

QStringList caveLines(const ChunkCounts &total)
{
    QString caves = u"== Caves: air below the soil layer"_s;
    for (std::size_t band = 0; band < bands.size(); ++band) {
        caves += u", y%1-%2 %3"_s.arg(bands[band].first).arg(bands[band].second - 1)
                     .arg(percent(static_cast<double>(total.caveAir[band]), static_cast<double>(total.cells[band])));
    }
    return {QString(),
            caves,
            u"== Dry columns whose ground a cave opens: %1"_s.arg(percent(static_cast<double>(total.openColumns), static_cast<double>(total.dryColumns), 2)),
            u"== Nearest lava lakes: %1"_s.arg(nearestPlaces(total.lavaLakes)),
            u"== Nearest cave entrances: %1"_s.arg(nearestPlaces(total.entrances))};
}

QStringList surfaceLines(const ChunkCounts &total, const QcTerrainGenerator &generator, const QcContentRegistry &content)
{
    QStringList lines{QString(), u"== Surface of dry land: top block per biome"_s};
    for (auto biome = total.surface.cbegin(); biome != total.surface.cend(); ++biome) {
        qint64 columns = 0;
        for (const qint64 count : *biome) {
            columns += count;
        }

        QList<QcBlockId> tops = biome->keys();
        std::ranges::sort(tops, [&](QcBlockId a, QcBlockId b) { return biome->value(a) > biome->value(b); });
        QStringList parts;
        for (const QcBlockId top : std::as_const(tops)) {
            parts << u"%1 %2"_s.arg(content.block(top).name, percent(static_cast<double>(biome->value(top)), static_cast<double>(columns)));
        }

        lines << u"%1 (%2 columns): %3"_s.arg(biomeLabel(generator, biome.key())).arg(columns).arg(parts.join(u", "_s));
    }

    return lines;
}

// Share, count, median and largest area, tiny regions and compactness of one biome's regions.
QString regionLine(const QString &label, QList<Region> list, qint64 cells)
{
    std::ranges::sort(list, {}, &Region::cells);
    qint64 area = 0;
    double compactWeighted = 0.0;
    for (const Region &region : std::as_const(list)) {
        area += region.cells;
        const double perimeter = static_cast<double>(region.edges);
        if (perimeter > 0.0) {
            compactWeighted += static_cast<double>(region.cells) * std::min(1.0,
                4.0 * std::numbers::pi * static_cast<double>(region.cells) / (perimeter * perimeter));
        }
    }

    const qint64 median = list[list.size() / 2].cells;
    // Speckles at borders: regions of fewer than four grid cells.
    const auto tiny = std::ranges::count_if(list, [&](const Region &region) { return region.cells < 4; });
    const double cellArea = static_cast<double>(biomeStep * biomeStep) / 1000.0;
    return u"%1 %2 %3 %4 %5 %6 %7"_s.arg(label, -14)
        .arg(percent(static_cast<double>(area), static_cast<double>(cells)), 7)
        .arg(list.size(), 8)
        .arg(static_cast<double>(median) * cellArea, 8, 'f', 0)
        .arg(static_cast<double>(list.constLast().cells) * cellArea, 9, 'f', 0)
        .arg(tiny, 8)
        .arg(compactWeighted / static_cast<double>(area), 8, 'f', 2);
}

QStringList regionLines(const QcTerrainGenerator &generator, int biomeRadius)
{
    const QList<Region> regions = biomeRegions(generator, biomeRadius);
    QMap<QString, QList<Region>> byBiome;
    qint64 cells = 0;
    for (const Region &region : regions) {
        byBiome[biomeLabel(generator, region.biome)].append(region);
        cells += region.cells;
    }

    QStringList lines{
        QString(),
        u"== Biome regions within %1 blocks (grid %2 blocks; area in 1000 blocks², compactness 1 = circle)"_s.arg(biomeRadius).arg(biomeStep)};
    lines << u"%1 %2 %3 %4 %5 %6 %7"_s.arg(u"biome"_s, -14).arg(u"share"_s, 7).arg(u"regions"_s, 8).arg(u"median"_s, 8)
                 .arg(u"largest"_s, 9).arg(u"tiny"_s, 8).arg(u"compact"_s, 8);
    for (auto it = byBiome.cbegin(); it != byBiome.cend(); ++it) {
        lines << regionLine(it.key(), it.value(), cells);
    }
    return lines;
}

struct TerrainStats
{
    qint64 columns = 0;
    std::array<qint64, 6> slope{};
    QList<int> heights;
};

// Steepest step and surface height of every fourth dry column around the origin, per biome.
QMap<QString, TerrainStats> sampleTerrain(const QcTerrainGenerator &generator)
{
    QMap<QString, TerrainStats> terrain;
    for (int z = -terrainRadius; z < terrainRadius; z += 4) {
        for (int x = -terrainRadius; x < terrainRadius; x += 4) {
            const QcTerrainGenerator::Column column = generator.column(x, z);
            if (column.height - 1 < generator.seaLevel()) {
                continue;
            }

            const int steepest = std::max({std::abs(generator.column(x + 1, z).height - column.height),
                                           std::abs(generator.column(x, z + 1).height - column.height)});
            TerrainStats &entry = terrain[biomeLabel(generator, column.biome)];
            entry.columns += 1;
            entry.slope[static_cast<std::size_t>(std::min(steepest, 5))] += 1;
            entry.heights.append(column.height - 1);
        }
    }

    return terrain;
}

QStringList terrainLines(const QcTerrainGenerator &generator)
{
    QMap<QString, TerrainStats> terrain = sampleTerrain(generator);
    QStringList lines{QString(), u"== Dry land within %1 blocks: steepest step to a neighbour, surface height"_s.arg(terrainRadius)};
    lines << u"%1 %2 %3 %4 %5 %6 %7 %8   %9"_s.arg(u"biome"_s, -14).arg(u"0"_s, 7).arg(u"1"_s, 7).arg(u"2"_s, 7).arg(u"3"_s, 7)
                 .arg(u"4"_s, 7).arg(u"5+"_s, 7).arg(u"cliff%"_s, 7).arg(u"height p5/p50/p95/max"_s);

    for (auto it = terrain.begin(); it != terrain.end(); ++it) {
        TerrainStats &entry = it.value();
        std::ranges::sort(entry.heights);
        const auto at = [&](double q) { return entry.heights[static_cast<qsizetype>(q * static_cast<double>(entry.heights.size() - 1))]; };

        QString line = u"%1"_s.arg(it.key(), -14);
        for (const qint64 count : entry.slope) {
            line += u" %1"_s.arg(percent(static_cast<double>(count), static_cast<double>(entry.columns)), 7);
        }
        line += u" %1   %2/%3/%4/%5"_s.arg(percent(static_cast<double>(entry.slope[3] + entry.slope[4] + entry.slope[5]),
            static_cast<double>(entry.columns)), 7)
                    .arg(at(0.05)).arg(at(0.5)).arg(at(0.95)).arg(entry.heights.constLast());
        lines << line;
    }

    return lines;
}

} // namespace

namespace QcWorldStats {

QStringList report(const QcTerrainGenerator &generator, const QcContentRegistry &content, int chunkRadius, int biomeRadius)
{
    QList<QcChunkPos> positions;
    for (int z = -chunkRadius; z <= chunkRadius; ++z) {
        for (int x = -chunkRadius; x <= chunkRadius; ++x) {
            positions.append({x, z});
        }
    }
    const ChunkCounts total = QtConcurrent::blockingMappedReduced<ChunkCounts>(
        positions, [&](QcChunkPos pos) { return ChunkCounter(generator, content, pos).count(); },
        [](ChunkCounts &sum, const ChunkCounts &part) { sum.add(part); });

    QStringList lines = undergroundLines(total, content, positions.size());
    lines << caveLines(total) << surfaceLines(total, generator, content);
    lines << regionLines(generator, biomeRadius) << terrainLines(generator);
    return lines;
}

} // namespace QcWorldStats
