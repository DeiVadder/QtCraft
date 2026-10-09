#include "QcWorldStats.h"

#include "QcContentRegistry.h"
#include "QcTerrainGenerator.h"

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
};

QString percent(double part, double whole, int precision = 1)
{
    return whole > 0.0 ? QString::number(100.0 * part / whole, 'f', precision) + u'%' : u"-"_s;
}

ChunkCounts countChunk(const QcTerrainGenerator &generator, const QcContentRegistry &content, QcChunkPos pos)
{
    ChunkCounts counts;
    const std::shared_ptr<QcChunk> chunk = generator.generate(pos);
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            const int x = pos.x * QcWorld::chunkSize + lx;
            const int z = pos.z * QcWorld::chunkSize + lz;
            const QcTerrainGenerator::Column column = generator.column(x, z);
            // Below the dirt or sand layer of the deepest biome.
            const int deepest = column.height - 6;
            for (int y = 1; y < deepest; ++y) {
                const QcBlockId block = chunk->block(lx, y, lz);
                for (std::size_t band = 0; band < bands.size(); ++band) {
                    if (y >= bands[band].first && y < bands[band].second) {
                        counts.cells[band] += 1;
                        counts.caveAir[band] += block == qcAirBlockId;
                    }
                }
                if (block == qcAirBlockId) {
                    continue;
                }
                counts.underground[block] += 1;
                for (std::size_t band = 0; band < bands.size(); ++band) {
                    if (y >= bands[band].first && y < bands[band].second) {
                        counts.perBand[band][block] += 1;
                    }
                }
                const auto air = [&](int dx, int dy, int dz) {
                    const int nx = lx + dx;
                    const int nz = lz + dz;
                    return nx >= 0 && nx < QcWorld::chunkSize && nz >= 0 && nz < QcWorld::chunkSize
                           && chunk->block(nx, y + dy, nz) == qcAirBlockId;
                };
                if (air(1, 0, 0) || air(-1, 0, 0) || air(0, 1, 0) || air(0, -1, 0) || air(0, 0, 1) || air(0, 0, -1)) {
                    counts.exposed[block] += 1;
                }
            }
            if (column.height - 1 >= generator.seaLevel()) {
                counts.dryColumns += 1;
                const int ground = generator.groundTop(x, z, column);
                if (chunk->block(lx, ground, lz) == qcAirBlockId) {
                    counts.openColumns += 1;
                    if (counts.entrances.isEmpty()) {
                        counts.entrances.append({x, ground, z});
                    }
                }
                const int top = chunk->highestNonAir(lx, lz);
                if (counts.lavaLakes.isEmpty() && content.block(chunk->block(lx, top, lz)).liquid && top >= generator.seaLevel()
                    && content.block(chunk->block(lx, top, lz)).name.endsWith(u"lava"_s)) {
                    counts.lavaLakes.append({x, top, z});
                }
                counts.surface[column.biome][chunk->block(lx, top, lz)] += 1;
            }
        }
    }
    return counts;
}

struct Region
{
    int biome = 0;
    qint64 cells = 0;
    qint64 edges = 0;
};

QList<Region> biomeRegions(const QcTerrainGenerator &generator, int radius)
{
    const int size = 2 * radius / biomeStep;
    QList<int> grid(qsizetype(size) * size);
    QtConcurrent::blockingMap(grid, [&](int &cell) {
        const qsizetype index = &cell - grid.data();
        const int x = -radius + int(index % size) * biomeStep;
        const int z = -radius + int(index / size) * biomeStep;
        const QcTerrainGenerator::Column column = generator.column(x, z);
        // Water does not split biomes: regions describe the climate map, the ocean share is extra.
        cell = column.landFactor < 0.5 ? oceanBiome : column.biome;
    });
    QList<int> label(grid.size(), -1);
    QList<Region> regions;
    QList<qsizetype> stack;
    for (qsizetype start = 0; start < grid.size(); ++start) {
        if (label[start] >= 0) {
            continue;
        }
        Region region;
        region.biome = grid[start];
        const int id = int(regions.size());
        label[start] = id;
        stack.append(start);
        while (!stack.isEmpty()) {
            const qsizetype cell = stack.takeLast();
            region.cells += 1;
            const int cx = int(cell % size);
            const int cz = int(cell / size);
            for (const auto &[dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                const int nx = cx + dx;
                const int nz = cz + dz;
                if (nx < 0 || nx >= size || nz < 0 || nz >= size) {
                    continue;
                }
                const qsizetype neighbour = qsizetype(nz) * size + nx;
                if (grid[neighbour] != region.biome) {
                    region.edges += 1;
                } else if (label[neighbour] < 0) {
                    label[neighbour] = id;
                    stack.append(neighbour);
                }
            }
        }
        regions.append(region);
    }
    return regions;
}

} // namespace

namespace QcWorldStats {

QStringList report(const QcTerrainGenerator &generator, const QcContentRegistry &content, int chunkRadius, int biomeRadius)
{
    QStringList lines;
    const auto name = [&](QcBlockId block) { return content.block(block).name; };
    const auto biomeLabel = [&](int biome) { return biome == oceanBiome ? u"ocean"_s : generator.biomeName(biome); };

    // --- Blocks in the generated chunks ---
    QList<QcChunkPos> positions;
    for (int z = -chunkRadius; z <= chunkRadius; ++z) {
        for (int x = -chunkRadius; x <= chunkRadius; ++x) {
            positions.append({x, z});
        }
    }
    const ChunkCounts total = QtConcurrent::blockingMappedReduced<ChunkCounts>(
        positions, [&](QcChunkPos pos) { return countChunk(generator, content, pos); },
        [](ChunkCounts &sum, const ChunkCounts &part) {
            for (auto it = part.underground.cbegin(); it != part.underground.cend(); ++it) {
                sum.underground[it.key()] += it.value();
            }
            for (auto it = part.exposed.cbegin(); it != part.exposed.cend(); ++it) {
                sum.exposed[it.key()] += it.value();
            }
            for (std::size_t band = 0; band < bands.size(); ++band) {
                for (auto it = part.perBand[band].cbegin(); it != part.perBand[band].cend(); ++it) {
                    sum.perBand[band][it.key()] += it.value();
                }
            }
            for (std::size_t band = 0; band < bands.size(); ++band) {
                sum.caveAir[band] += part.caveAir[band];
                sum.cells[band] += part.cells[band];
            }
            sum.dryColumns += part.dryColumns;
            sum.openColumns += part.openColumns;
            sum.lavaLakes.append(part.lavaLakes);
            sum.entrances.append(part.entrances);
            for (auto biome = part.surface.cbegin(); biome != part.surface.cend(); ++biome) {
                for (auto it = biome->cbegin(); it != biome->cend(); ++it) {
                    sum.surface[biome.key()][it.key()] += it.value();
                }
            }
        });

    lines << u"== Underground (%1 chunks, below the soil layer; per mille of solid blocks)"_s.arg(positions.size());
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
        const auto perMille = [](qint64 part, qint64 whole) {
            return whole > 0 ? QString::number(1000.0 * double(part) / double(whole), 'f', 2) : u"-"_s;
        };
        QString line = u"%1 %2 %3 %4"_s.arg(name(block), -16)
                           .arg(perMille(total.underground[block], solid), 8)
                           .arg(perMille(total.exposed.value(block), exposedSolid), 8)
                           .arg(double(total.underground[block]) / double(positions.size()), 10, 'f', 1);
        for (std::size_t band = 0; band < bands.size(); ++band) {
            qint64 bandSolid = 0;
            for (const qint64 count : total.perBand[band]) {
                bandSolid += count;
            }
            line += u"%1"_s.arg(perMille(total.perBand[band].value(block), bandSolid), 9);
        }
        lines << line;
    }

    QString caves = u"== Caves: air below the soil layer"_s;
    for (std::size_t band = 0; band < bands.size(); ++band) {
        caves += u", y%1-%2 %3"_s.arg(bands[band].first).arg(bands[band].second - 1)
                     .arg(percent(double(total.caveAir[band]), double(total.cells[band])));
    }
    lines << QString() << caves
          << u"== Dry columns whose ground a cave opens: %1"_s.arg(percent(double(total.openColumns), double(total.dryColumns), 2));
    const auto places = [](const QList<QcBlockPos> &found) {
        QList<QcBlockPos> list = found;
        std::ranges::sort(list, {}, [](QcBlockPos pos) { return qint64(pos.x) * pos.x + qint64(pos.z) * pos.z; });
        QStringList parts;
        const QList<QcBlockPos> nearest = list.first(std::min<qsizetype>(list.size(), 6));
        for (const QcBlockPos &pos : nearest) {
            parts << u"%1,%2,%3"_s.arg(pos.x).arg(pos.y).arg(pos.z);
        }
        return parts.join(u"  "_s);
    };
    lines << u"== Nearest lava lakes: %1"_s.arg(places(total.lavaLakes)) << u"== Nearest cave entrances: %1"_s.arg(places(total.entrances));
    lines << QString() << u"== Surface of dry land: top block per biome"_s;
    for (auto biome = total.surface.cbegin(); biome != total.surface.cend(); ++biome) {
        qint64 columns = 0;
        for (const qint64 count : *biome) {
            columns += count;
        }
        QList<QcBlockId> tops = biome->keys();
        std::ranges::sort(tops, [&](QcBlockId a, QcBlockId b) { return biome->value(a) > biome->value(b); });
        QStringList parts;
        for (const QcBlockId top : std::as_const(tops)) {
            parts << u"%1 %2"_s.arg(name(top), percent(double(biome->value(top)), double(columns)));
        }
        lines << u"%1 (%2 columns): %3"_s.arg(biomeLabel(biome.key())).arg(columns).arg(parts.join(u", "_s));
    }

    // --- Biome regions ---
    const QList<Region> regions = biomeRegions(generator, biomeRadius);
    QMap<QString, QList<Region>> byBiome;
    qint64 cells = 0;
    for (const Region &region : regions) {
        byBiome[biomeLabel(region.biome)].append(region);
        cells += region.cells;
    }
    lines << QString()
          << u"== Biome regions within %1 blocks (grid %2 blocks; area in 1000 blocks², compactness 1 = circle)"_s.arg(biomeRadius).arg(biomeStep);
    lines << u"%1 %2 %3 %4 %5 %6 %7"_s.arg(u"biome"_s, -14).arg(u"share"_s, 7).arg(u"regions"_s, 8).arg(u"median"_s, 8)
                 .arg(u"largest"_s, 9).arg(u"tiny"_s, 8).arg(u"compact"_s, 8);
    for (auto it = byBiome.cbegin(); it != byBiome.cend(); ++it) {
        QList<Region> list = it.value();
        std::ranges::sort(list, {}, &Region::cells);
        qint64 area = 0;
        double compactWeighted = 0.0;
        for (const Region &region : std::as_const(list)) {
            area += region.cells;
            const double perimeter = double(region.edges);
            if (perimeter > 0.0) {
                compactWeighted += double(region.cells) * std::min(1.0, 4.0 * std::numbers::pi * double(region.cells) / (perimeter * perimeter));
            }
        }
        const qint64 median = list[list.size() / 2].cells;
        // Speckles at borders: regions of fewer than four grid cells.
        const auto tiny = std::ranges::count_if(list, [&](const Region &region) { return region.cells < 4; });
        const double cellArea = double(biomeStep * biomeStep) / 1000.0;
        lines << u"%1 %2 %3 %4 %5 %6 %7"_s.arg(it.key(), -14)
                     .arg(percent(double(area), double(cells)), 7)
                     .arg(list.size(), 8)
                     .arg(double(median) * cellArea, 8, 'f', 0)
                     .arg(double(list.constLast().cells) * cellArea, 9, 'f', 0)
                     .arg(tiny, 8)
                     .arg(compactWeighted / double(area), 8, 'f', 2);
    }

    // --- Slopes and heights of dry land ---
    struct Terrain
    {
        qint64 columns = 0;
        std::array<qint64, 6> slope{};
        QList<int> heights;
    };
    QMap<QString, Terrain> terrain;
    for (int z = -terrainRadius; z < terrainRadius; z += 4) {
        for (int x = -terrainRadius; x < terrainRadius; x += 4) {
            const QcTerrainGenerator::Column column = generator.column(x, z);
            if (column.height - 1 < generator.seaLevel()) {
                continue;
            }
            const int steepest = std::max({std::abs(generator.column(x + 1, z).height - column.height),
                                           std::abs(generator.column(x, z + 1).height - column.height)});
            Terrain &entry = terrain[biomeLabel(column.biome)];
            entry.columns += 1;
            entry.slope[std::size_t(std::min(steepest, 5))] += 1;
            entry.heights.append(column.height - 1);
        }
    }
    lines << QString() << u"== Dry land within %1 blocks: steepest step to a neighbour, surface height"_s.arg(terrainRadius);
    lines << u"%1 %2 %3 %4 %5 %6 %7 %8   %9"_s.arg(u"biome"_s, -14).arg(u"0"_s, 7).arg(u"1"_s, 7).arg(u"2"_s, 7).arg(u"3"_s, 7)
                 .arg(u"4"_s, 7).arg(u"5+"_s, 7).arg(u"cliff%"_s, 7).arg(u"height p5/p50/p95/max"_s);
    for (auto it = terrain.begin(); it != terrain.end(); ++it) {
        Terrain &entry = it.value();
        std::ranges::sort(entry.heights);
        const auto at = [&](double q) { return entry.heights[qsizetype(q * double(entry.heights.size() - 1))]; };
        QString line = u"%1"_s.arg(it.key(), -14);
        for (const qint64 count : entry.slope) {
            line += u" %1"_s.arg(percent(double(count), double(entry.columns)), 7);
        }
        line += u" %1   %2/%3/%4/%5"_s.arg(percent(double(entry.slope[3] + entry.slope[4] + entry.slope[5]), double(entry.columns)), 7)
                    .arg(at(0.05)).arg(at(0.5)).arg(at(0.95)).arg(entry.heights.constLast());
        lines << line;
    }
    return lines;
}

} // namespace QcWorldStats
