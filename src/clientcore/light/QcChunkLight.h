#pragma once

#include "QcChunkSection.h"
#include "QcWorldConstants.h"

#include <QtGlobal>

#include <array>
#include <memory>

// Block light levels (0..15) of one chunk as immutable section snapshots; a missing section is dark.
// QcBlockLightEngine produces them, the mesher and the entity lighting read them.
struct QcChunkLight
{
    using Section = std::array<quint8, QcWorld::blocksPerSection>;

    std::array<std::shared_ptr<const Section>, QcWorld::sectionCount> sections;

    // Local x and z in [0, 16); y outside the world reads dark.
    int level(int x, int y, int z) const
    {
        if (y < 0 || y >= QcWorld::worldHeight) {
            return 0;
        }

        const std::shared_ptr<const Section> &section = sections[std::size_t(y / QcWorld::sectionHeight)];
        return section ? (*section)[std::size_t(QcChunkSection::index(x, y % QcWorld::sectionHeight, z))] : 0;
    }
};
