// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/generators/QcBiomeMap.h"

#include <optional>

class QcContentRegistry;
class QcNetherGenerator;

/*! \brief The map of a dimension under a roof for the map preview of a new world (decision E-1077):
    it shows no surface from above but the biome of each cell, exactly as
    QcNetherGenerator::biomeAt() decides it, and as sites what the generator's content places.

    The cells are QcBiomeMap cells of land, all at the height of the lava sea; their biome is the
    biome's index in the content (as on the overworld's map), so the colours and names of the map
    and its legend come from the content. */
namespace QcNetherMap {

/*! Returns the map of \a area with the biome of each cell's north-west column, as \a generator
    decides it, as the index of the biome in \a content; none when \a cancelled said so first. */
[[nodiscard]] std::optional<QcBiomeMap> sample(const QcNetherGenerator &generator, const QcContentRegistry &content, QcBiomeMap::Area area,
                                               const QcBiomeMap::Cancelled &cancelled = {});
/*! Returns what \a generator can place, as worldgen/nether.json says, in a fixed order (a kind's
    place is its marker on the map): the fortress when the content has one, then the structures of
    the file, a mod's too, each at the place /locate names. */
[[nodiscard]] QList<QcBiomeMap::SiteRule> siteRules(const QcNetherGenerator &generator);

} // namespace QcNetherMap
