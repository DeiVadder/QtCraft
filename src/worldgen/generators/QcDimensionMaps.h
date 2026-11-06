// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/generators/QcBiomeMap.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

class QcContentRegistry;
class QcWorldGenerator;
struct QcDimensionDefinition;

/*! \brief Which map a dimension gets in the map preview of a new world (decision E-1078): a small
    table from the type of a dimension's generator to its kind of map, the overworld's map for
    terrain and the map under a roof for the nether's generator. A mod's dimension of one of these
    types has its map; one of another type (flat, void) or with options the server refuses has
    none and stays out of the switch. */
namespace QcDimensionMaps {

/*! \brief What the passes of a map take from a generator, which must outlive it: \c {map} draws
    an area, \c {sites} finds the sites in one, \c {kinds} are the kinds of sites in the order of
    their markers, \c {seaLevel} is the height the painter takes for the sea, none for a map without
    relief, whose cells show the colours of their biomes as they are. \qcfields */
struct Sampler
{
    std::function<std::optional<QcBiomeMap>(QcBiomeMap::Area, const QcBiomeMap::Cancelled &)> map;
    std::function<std::optional<QList<QcBiomeMap::Site>>(QcBiomeMap::Area, const QcBiomeMap::Cancelled &)> sites;
    QStringList kinds;
    std::optional<int> seaLevel;
};

/*! Returns the dimension \a name of \a content, the overworld also when the content does not define
    it (QcWorldgenCatalog::overworld()); null when there is none. */
[[nodiscard]] const QcDimensionDefinition *dimensionOf(const QcContentRegistry &content, const QString &name);
/*! Returns whether \a dimension of \a content has a map: the type of its generator has one, and the
    server opens it (QcWorldGenerators::checkOptions). */
[[nodiscard]] bool hasMap(const QcDimensionDefinition &dimension, const QcContentRegistry &content);
/*! Returns the sampler of \a dimension, whose map exists, with \a generator, which was made for
    it, and \a content, which outlives it; biomes are indices of \a content. */
[[nodiscard]] Sampler samplerOf(const QcDimensionDefinition &dimension, const QcWorldGenerator &generator, const QcContentRegistry &content);

} // namespace QcDimensionMaps
