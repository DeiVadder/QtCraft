// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/nether/QcNetherMap.h"

#include "common/content/registry/QcContentRegistry.h"
#include "worldgen/nether/QcNetherGenerator.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// The index in `content` of each biome of `generator` (-1 for one the content lacks).
QList<int> contentIndices(const QcNetherGenerator &generator, const QcContentRegistry &content)
{
    const QList<QcBiomeDefinition> &biomes = content.worldgen().biomes();
    QList<int> indices;
    for (const QcNetherDefinitions::Biome &biome : generator.definitions().biomes) {
        const auto found = std::ranges::find(biomes, biome.name, &QcBiomeDefinition::name);
        indices.append(found == biomes.end() ? -1 : static_cast<int>(found - biomes.begin()));
    }
    return indices;
}

// The rule of a great structure: the plans `planOf` gives that are built, at the place /locate
// finds.
template <typename PlanOf>
QcBiomeMap::SiteRule greatRule(const QString &kind, int regionSize, PlanOf planOf)
{
    const auto placeOf = [kind, planOf](int regionX, int regionZ) -> std::optional<QcBiomeMap::Site> {
        const auto plan = planOf(regionX, regionZ);
        if (!plan) {
            return std::nullopt;
        }
        const QcBlockPos middle = plan->middle();
        return QcBiomeMap::Site{QcBiomeMap::Site::Kind::Structure, middle.x, middle.z, kind};
    };
    return {kind, regionSize, placeOf};
}

// The rule of one kind of structure of the file.
QcBiomeMap::SiteRule structureRule(const QcNetherGenerator &generator, const QcNetherDefinitions::Structure &structure)
{
    const auto placeOf = [&generator, &structure](int regionX, int regionZ) -> std::optional<QcBiomeMap::Site> {
        const std::optional<QcStructurePlacement> plan = generator.structures().planOf(structure, regionX, regionZ);
        if (!plan) {
            return std::nullopt;
        }
        const QcBlockPos middle = plan->middle();
        return QcBiomeMap::Site{QcBiomeMap::Site::Kind::Structure, middle.x, middle.z, structure.name};
    };
    return {structure.name, structure.spacing, placeOf};
}

} // namespace

namespace QcNetherMap {

std::optional<QcBiomeMap> sample(const QcNetherGenerator &generator, const QcContentRegistry &content, QcBiomeMap::Area area,
                                 const QcBiomeMap::Cancelled &cancelled)
{
    const QList<int> indices = contentIndices(generator, content);
    const int height = generator.definitions().settings.liquidLevel;
    const auto cellOf = [&](int x, int z) {
        // The same index QcNetherGenerator::biomeAt() names.
        const int biome = generator.terrain().biomeAt(x, z);
        return QcBiomeMap::Cell{biome >= 0 && biome < indices.size() ? indices.at(biome) : -1, height, QcBiomeMap::Water::Land, false};
    };
    return QcBiomeMap::sampleWith(area, cellOf, cancelled);
}

QList<QcBiomeMap::SiteRule> siteRules(const QcNetherGenerator &generator)
{
    QList<QcBiomeMap::SiteRule> rules;
    if (generator.definitions().settings.fortress.regionSize > 0) {
        const auto planOf = [&generator](int regionX, int regionZ) { return generator.fortress().planOf(regionX, regionZ); };
        rules.append(greatRule(u"qtcraft:fortress"_s, generator.definitions().settings.fortress.regionSize, planOf));
    }
    if (generator.definitions().settings.bastion.regionSize > 0) {
        const auto planOf = [&generator](int regionX, int regionZ) { return generator.bastion().planOf(regionX, regionZ); };
        rules.append(greatRule(u"qtcraft:bastion"_s, generator.definitions().settings.bastion.regionSize, planOf));
    }
    for (const QcNetherDefinitions::Structure &structure : generator.definitions().structures) {
        rules.append(structureRule(generator, structure));
    }
    return rules;
}

} // namespace QcNetherMap
