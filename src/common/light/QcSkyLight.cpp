#include "common/light/QcSkyLight.h"

#include "common/world/QcLightPassage.h"

namespace QcSkyLight {

int columnTop(const QcChunk &blocks, const QcLightTable &table, int lx, int lz)
{
    for (int sectionIndex = QcWorld::sectionCount - 1; sectionIndex >= 0; --sectionIndex) {
        const QcChunkSection &section = blocks.section(sectionIndex);
        if (section.isEmpty()) {
            continue;
        }
        for (int ly = QcWorld::sectionHeight - 1; ly >= 0; --ly) {
            const QcLightPassage::SkyStop stop = table.entry(section.block(lx, ly, lz)).skyStop;
            if (stop != QcLightPassage::SkyStop::None) {
                return QcLightPassage::skyTop(stop, sectionIndex * QcWorld::sectionHeight + ly);
            }
        }
    }
    return -1;
}

Tops tops(const QcChunk &blocks, const QcLightTable &table)
{
    Tops result{};
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            result[static_cast<std::size_t>(QcLightCells::columnIndex(lx, lz))] = static_cast<qint16>(columnTop(blocks, table, lx, lz));
        }
    }
    return result;
}

std::unique_ptr<QcLightNibbles> sectionCells(const Tops &tops, int low, int high)
{
    auto cells = std::make_unique<QcLightNibbles>(0);
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            const int top = tops[static_cast<std::size_t>(QcLightCells::columnIndex(lx, lz))];
            for (int y = std::max(low, top + 1); y <= high; ++y) {
                cells->setLevel(QcChunkSection::index(lx, y - low, lz), QcLightTable::maxLevel);
            }
        }
    }
    return cells;
}

} // namespace QcSkyLight
