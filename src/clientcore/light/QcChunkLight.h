// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcWorldConstants.h"
#include "common/light/QcLightNibbles.h"
#include "common/world/QcChunkSection.h"

#include <QtGlobal>

#include <array>
#include <memory>

// Block light levels (0..15) of one chunk as immutable section snapshots; a missing section is dark.
// QcBlockLightEngine produces them (from the shared QcLightEngine), the mesher and the entity lighting read them.
struct QcChunkLight
{
    using Section = QcLightNibbles;

    std::array<std::shared_ptr<const Section>, QcWorld::sectionCount> sections;

    // Local x and z in [0, 16); y outside the world reads dark.
    int level(int x, int y, int z) const
    {
        if (y < 0 || y >= QcWorld::worldHeight) {
            return 0;
        }

        const std::shared_ptr<const Section> &section = sections[static_cast<std::size_t>(y / QcWorld::sectionHeight)];
        return section ? section->level(QcChunkSection::index(x, y % QcWorld::sectionHeight, z)) : 0;
    }
};
