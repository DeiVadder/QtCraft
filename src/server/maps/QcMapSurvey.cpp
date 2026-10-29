// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/maps/QcMapSurvey.h"

#include "common/maps/QcMapPixel.h"
#include "server/core/QcServerWorld.h"

#include <QVarLengthArray>

#include <cmath>

namespace {

// The top of one block column as a map sees it.
struct Column
{
    int height = -1;
    quint32 block = 0;
    // Liquid blocks from the top down (0 on land).
    int depth = 0;
};

// What the columns of one pixel add up to.
struct Area
{
    // Mean height of the columns with a block; negative when none has one (a blank pixel).
    double height = -1.0;
    // The most common top block.
    quint32 block = 0;
    // Whether that block is a liquid, and the mean liquid depth over all columns.
    bool liquid = false;
    double depth = 0.0;
};

// How often each top block occurs among the columns of a pixel.
class BlockCounts
{
public:
    void add(Column column)
    {
        for (Count &count : m_counts) {
            if (count.block == column.block) {
                ++count.count;
                return;
            }
        }
        m_counts.append({column.block, 1, column.depth > 0});
    }

    // The block seen most often (the first of equally common ones) and whether it is a liquid.
    [[nodiscard]] std::pair<quint32, bool> mostCommon() const
    {
        const Count *best = &m_counts.front();
        for (const Count &count : m_counts) {
            if (count.count > best->count) {
                best = &count;
            }
        }
        return {best->block, best->liquid};
    }

private:
    struct Count
    {
        quint32 block = 0;
        int count = 0;
        bool liquid = false;
    };
    QVarLengthArray<Count, 8> m_counts;
};

// Reads the block columns of map pixels, keeping the chunk of the last one (the columns of a pixel
// lie mostly in one chunk).
class Columns
{
public:
    explicit Columns(const QcServerWorld &world)
        : m_world(world)
    {
    }

    // What the columns of `pixel` of `map` add up to; std::nullopt when one is not loaded.
    [[nodiscard]] std::optional<Area> areaOf(const QcMapData &map, QPoint pixel)
    {
        const int side = map.blocksPerPixel();
        const QPoint corner = map.columnOf(pixel);
        BlockCounts counts;
        int filled = 0;
        qint64 heights = 0;
        qint64 depths = 0;
        for (int dz = 0; dz < side; ++dz) {
            for (int dx = 0; dx < side; ++dx) {
                const std::optional<Column> top = at(corner + QPoint(dx, dz));
                if (!top) {
                    return std::nullopt;
                }
                if (top->height < 0) {
                    continue;
                }
                ++filled;
                heights += top->height;
                depths += top->depth;
                counts.add(*top);
            }
        }

        if (filled == 0) {
            return Area{};
        }
        const auto [block, liquid] = counts.mostCommon();
        return Area{static_cast<double>(heights) / filled, block, liquid, static_cast<double>(depths) / (side * side)};
    }

private:
    // The top of the column (x, z); std::nullopt when its chunk is not loaded.
    [[nodiscard]] std::optional<Column> at(QPoint column)
    {
        const QcBlockPos pos{column.x(), 0, column.y()};
        if (!m_chunk || pos.chunk() != m_chunkPos) {
            m_chunkPos = pos.chunk();
            m_chunk = m_world.chunk(m_chunkPos);
        }
        if (!m_chunk) {
            return std::nullopt;
        }
        return topOf(*m_chunk, pos.localX(), pos.localZ());
    }

    [[nodiscard]] Column topOf(const QcChunk &chunk, int x, int z) const
    {
        const QcContentRegistry &content = m_world.content();
        Column top;
        int y = chunk.highestNonAir(x, z);
        while (y >= 0 && content.block(chunk.block(x, y, z)).renderLayer == QcRenderLayer::Invisible) {
            --y;
        }
        if (y < 0) {
            return top;
        }
        top.height = y;
        top.block = content.state(chunk.block(x, y, z)).type;
        while (y - top.depth >= 0 && content.block(chunk.block(x, y - top.depth, z)).liquid) {
            ++top.depth;
        }
        return top;
    }

    const QcServerWorld &m_world;
    QcChunkPos m_chunkPos;
    std::shared_ptr<const QcChunk> m_chunk;
};

QcMapPixel::Shade shadeOf(const Area &area, const std::optional<Area> &north, QPoint pixel, int blocksPerPixel)
{
    using enum QcMapPixel::Shade;
    const int checker = (pixel.x() + pixel.y()) & 1;
    if (area.liquid) {
        const double depth = std::min(area.depth, static_cast<double>(QcMapSurvey::deepWater)) * 0.1 + checker * 0.2;
        return depth < 0.5 ? Bright : depth > 0.9 ? Dark : Normal;
    }
    if (!north || north->height < 0) {
        return Normal;
    }
    const double slope = (area.height - north->height) * 4.0 / (blocksPerPixel + 4) + (checker - 0.5) * 0.4;
    return slope > 0.6 ? Bright : slope < -0.6 ? Dark : Normal;
}

quint16 valueOf(const Area &area, const std::optional<Area> &north, QPoint pixel, int blocksPerPixel)
{
    if (area.height < 0) {
        return QcMapPixel::blank;
    }
    return QcMapPixel::make(area.block, shadeOf(area, north, pixel, blocksPerPixel));
}

} // namespace

namespace QcMapSurvey {

QList<QPoint> pixelsDue(const QcMapData &map, QPointF holder, quint64 tick)
{
    const int scale = map.blocksPerPixel();
    const double radius = static_cast<double>(revealRadius) / scale;
    const QPointF offset = map.offsetOf(holder.x(), holder.y()) / 2.0;
    const QPointF middle = offset + QPointF(QcMapData::size / 2.0, QcMapData::size / 2.0);
    const int phase = static_cast<int>(tick % sweep);

    QList<QPoint> due;
    const int left = std::max(0, static_cast<int>(std::floor(middle.x() - radius)));
    const int right = std::min(QcMapData::size - 1, static_cast<int>(std::ceil(middle.x() + radius)));
    const int top = std::max(0, static_cast<int>(std::floor(middle.y() - radius)));
    const int bottom = std::min(QcMapData::size - 1, static_cast<int>(std::ceil(middle.y() + radius)));
    for (int x = left + ((phase - left) % sweep + sweep) % sweep; x <= right; x += sweep) {
        for (int y = top; y <= bottom; ++y) {
            const double dx = x + 0.5 - middle.x();
            const double dy = y + 0.5 - middle.y();
            if (dx * dx + dy * dy <= radius * radius) {
                due.append({x, y});
            }
        }
    }
    return due;
}

std::optional<quint16> pixelOf(const QcServerWorld &world, const QcMapData &map, QPoint pixel)
{
    Columns columns(world);
    const std::optional<Area> area = columns.areaOf(map, pixel);
    if (!area) {
        return std::nullopt;
    }
    const std::optional<Area> north = columns.areaOf(map, pixel - QPoint(0, 1));
    return valueOf(*area, north, pixel, map.blocksPerPixel());
}

QList<Surveyed> survey(const QcServerWorld &world, const QcMapData &map, const QList<QPoint> &pixels)
{
    Columns columns(world);
    QList<Surveyed> shown;
    // The area of the pixel before, the north neighbour of the next one in the same column.
    std::optional<QPoint> previous;
    std::optional<Area> previousArea;
    for (const QPoint pixel : pixels) {
        const QPoint northPixel = pixel - QPoint(0, 1);
        const std::optional<Area> north = previous == northPixel ? previousArea : columns.areaOf(map, northPixel);
        const std::optional<Area> area = columns.areaOf(map, pixel);
        previous = pixel;
        previousArea = area;
        if (area) {
            shown.append({pixel, valueOf(*area, north, pixel, map.blocksPerPixel())});
        }
    }
    return shown;
}

} // namespace QcMapSurvey
