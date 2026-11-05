// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcWorldConstants.h"
#include "common/light/QcLightNibbles.h"
#include "common/world/QcChunkSection.h"

#include <QtGlobal>

#include <array>
#include <memory>

/*! \brief Block light levels (0..15) of one chunk as immutable section snapshots; a missing section
    is dark.

    QcBlockLightEngine produces them (from the shared QcLightEngine), the mesher and the entity
    lighting read them. */
struct QcChunkLight
{
    /*! Light levels of one section, immutable once published. */
    using Section = QcLightNibbles;

    /*! Sections from the bottom up; null where the section is dark. */
    std::array<std::shared_ptr<const Section>, QcWorld::sectionCount> sections;

    /*! Returns the level of the cell at local \a x and \a z in [0, 16) and height \a y; outside
        the world dark. */
    int level(int x, int y, int z) const
    {
        if (y < 0 || y >= QcWorld::worldHeight) {
            return 0;
        }

        const std::shared_ptr<const Section> &section = sections[static_cast<std::size_t>(y / QcWorld::sectionHeight)];
        return section ? section->level(QcChunkSection::index(x, y % QcWorld::sectionHeight, z)) : 0;
    }
};
