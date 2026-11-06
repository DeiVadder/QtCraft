// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/generators/QcDimensionMaps.h"

#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/core/QcDowncast.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/nether/QcNetherGenerator.h"
#include "worldgen/nether/QcNetherMap.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <algorithm>
#include <array>

namespace {

using Sampler = QcDimensionMaps::Sampler;

Sampler overworldMap(const QcWorldGenerator &generator, const QcContentRegistry &)
{
    const QcTerrainGenerator &terrain = *qcDowncast<const QcTerrainGenerator>(&generator);
    return {[&terrain](QcBiomeMap::Area area, const QcBiomeMap::Cancelled &cancelled) { return QcBiomeMap::sample(terrain, area, cancelled); },
            [&terrain](QcBiomeMap::Area area, const QcBiomeMap::Cancelled &cancelled) { return QcBiomeMap::sites(terrain, area, cancelled); },
            QcBiomeMap::siteKinds(terrain), terrain.seaLevel()};
}

Sampler mapUnderARoof(const QcWorldGenerator &generator, const QcContentRegistry &content)
{
    const QcNetherGenerator &nether = *qcDowncast<const QcNetherGenerator>(&generator);
    const QList<QcBiomeMap::SiteRule> rules = QcNetherMap::siteRules(nether);
    QStringList kinds;
    for (const QcBiomeMap::SiteRule &rule : rules) {
        kinds.append(rule.kind);
    }
    return {[&nether, &content](QcBiomeMap::Area area, const QcBiomeMap::Cancelled &cancelled) { return QcNetherMap::sample(nether, content, area, cancelled); },
            [rules](QcBiomeMap::Area area, const QcBiomeMap::Cancelled &cancelled) { return QcBiomeMap::sitesOf(rules, area, cancelled); },
            kinds, std::nullopt};
}

struct MapKind
{
    const char *generatorType;
    Sampler (*make)(const QcWorldGenerator &, const QcContentRegistry &);
};

constexpr std::array mapKinds{MapKind{QcWorldGenerators::terrain, overworldMap}, MapKind{QcWorldGenerators::nether, mapUnderARoof}};

const MapKind *kindOf(const QcDimensionDefinition &dimension)
{
    // The preview shows new terrain worlds: their overworld generates as the world's own type.
    const QString type = dimension.isOverworld() ? QString::fromLatin1(QcWorldGenerators::terrain) : dimension.generatorType;
    const auto found = std::ranges::find_if(mapKinds, [&type](MapKind kind) { return type == QLatin1StringView(kind.generatorType); });
    return found == mapKinds.end() ? nullptr : &*found;
}

} // namespace

namespace QcDimensionMaps {

const QcDimensionDefinition *dimensionOf(const QcContentRegistry &content, const QString &name)
{
    const bool overworld = name == QLatin1StringView(QcDimensionDefinition::overworldName);
    return overworld ? &content.worldgen().overworld() : content.worldgen().dimension(name);
}

bool hasMap(const QcDimensionDefinition &dimension, const QcContentRegistry &content)
{
    if (kindOf(dimension) == nullptr) {
        return false;
    }
    // The overworld is open from the start; any other opens only with options its generator takes.
    return dimension.isOverworld() || static_cast<bool>(QcWorldGenerators::checkOptions(dimension.generatorType, dimension.generatorOptions, content));
}

Sampler samplerOf(const QcDimensionDefinition &dimension, const QcWorldGenerator &generator, const QcContentRegistry &content)
{
    const MapKind *kind = kindOf(dimension);
    return kind ? kind->make(generator, content) : Sampler{};
}

} // namespace QcDimensionMaps
