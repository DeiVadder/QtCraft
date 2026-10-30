// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStringList>

class QcContentRegistry;
class QcTerrainGenerator;

/*! \brief Development aid: numbers that describe a generated world, used to tune the
    world-generation data without playing.

    Generates the chunks around the origin and reports the share of each block below ground (ores
    against stone), what stands on grass (flowers, grass, nothing), the share and size of biome
    regions, slopes and cliffs, and heights. */
namespace QcWorldStats {

/*! Reports on the world \a generator makes with \a content: \a chunkRadius chunks around the
    origin are generated; biome regions are sampled over
    \a biomeRadius blocks. Returns the report as lines of text. */
QStringList report(const QcTerrainGenerator &generator, const QcContentRegistry &content, int chunkRadius, int biomeRadius);

} // namespace QcWorldStats
