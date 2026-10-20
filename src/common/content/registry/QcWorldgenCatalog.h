#pragma once

#include "common/content/definitions/QcShowcaseDefinition.h"
#include "common/content/definitions/QcStructureDefinition.h"
#include "common/content/definitions/QcWorldgenDefinitions.h"
#include "common/content/definitions/QcWorldTypeDefinitions.h"
#include "common/content/registry/QcNamedList.h"

// What the world generators build from (part of QcContentRegistry, server side only, not
// synchronised): biomes, features, ores and the terrain settings, the structures and the settings of
// the showcase, flat and void world types. Definitions are replaced by name, settings by a later pack.
class QcWorldgenCatalog
{
public:
    void registerBiome(QcBiomeDefinition definition) { m_biomes.set(std::move(definition)); }
    const QList<QcBiomeDefinition> &biomes() const { return m_biomes.items(); }
    // The biome named `name`; null when there is none.
    const QcBiomeDefinition *biome(const QString &name) const { return m_biomes.find(name); }
    void registerFeature(QcFeatureDefinition definition) { m_features.set(std::move(definition)); }
    const QList<QcFeatureDefinition> &features() const { return m_features.items(); }
    void registerOre(QcOreDefinition definition) { m_ores.set(std::move(definition)); }
    const QList<QcOreDefinition> &ores() const { return m_ores.items(); }
    void setTerrainSettings(QcTerrainSettings settings) { m_terrain = std::move(settings); }
    const QcTerrainSettings &terrainSettings() const { return m_terrain; }

    void registerStructure(QcStructureDefinition structure) { m_structures.set(std::move(structure)); }
    const QList<QcStructureDefinition> &structures() const { return m_structures.items(); }
    const QcStructureDefinition *structure(const QString &name) const { return m_structures.find(name); }

    void setShowcase(QcShowcaseDefinition showcase) { m_showcase = std::move(showcase); }
    const QcShowcaseDefinition &showcase() const { return m_showcase; }
    void setFlatWorld(QcFlatDefinition flat) { m_flatWorld = std::move(flat); }
    const QcFlatDefinition &flatWorld() const { return m_flatWorld; }
    void setVoidWorld(QcVoidDefinition empty) { m_voidWorld = std::move(empty); }
    const QcVoidDefinition &voidWorld() const { return m_voidWorld; }

private:
    QcNamedList<QcBiomeDefinition> m_biomes;
    QcNamedList<QcFeatureDefinition> m_features;
    QcNamedList<QcOreDefinition> m_ores;
    QcTerrainSettings m_terrain;
    QcNamedList<QcStructureDefinition> m_structures;
    QcShowcaseDefinition m_showcase;
    QcFlatDefinition m_flatWorld;
    QcVoidDefinition m_voidWorld;
};
