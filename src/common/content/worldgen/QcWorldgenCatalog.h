// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcNamedList.h"
#include "common/content/worldgen/QcShowcaseDefinition.h"
#include "common/content/worldgen/QcStructureDefinition.h"
#include "common/content/worldgen/QcWorldgenDefinitions.h"
#include "common/content/worldgen/QcWorldOptionDefinitions.h"
#include "common/content/worldgen/QcWorldTypeDefinitions.h"

// What the world generators build from (part of QcContentRegistry, server side only, not
// synchronised): biomes, features, ores and the terrain settings, the structures and the settings of
// the showcase, flat and void world types, and the options a world may set on them (decision E-917).
// Definitions are replaced by name, settings by a later pack.
class QcWorldgenCatalog
{
public:
    void registerBiome(QcBiomeDefinition definition) { m_biomes.set(std::move(definition)); }
    const QList<QcBiomeDefinition> &biomes() const { return m_biomes.items(); }
    void removeBiome(const QString &name) { m_biomes.remove(name); }
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

    // The world generation options (decision E-917), in the order of the packs and their files.
    void addWorldOptions(const QcWorldOptionSet &set);
    const QList<QcWorldOptionGroup> &worldOptionGroups() const { return m_optionGroups.items(); }
    const QList<QcWorldOptionDefinition> &worldOptions() const { return optionList().items(); }
    const QcWorldOptionDefinition *worldOption(const QString &name) const { return optionList().find(name); }
    const QList<QcWorldPreset> &worldPresets() const { return presetList().items(); }
    const QcWorldPreset *worldPreset(const QString &name) const { return presetList().find(name); }

private:
    const QcNamedList<QcWorldOptionDefinition> &optionList() const { return m_options; }
    void setOption(const QcWorldOptionDefinition &option) { m_options.set(option); }
    const QcNamedList<QcWorldPreset> &presetList() const { return m_presets; }
    void setPreset(const QcWorldPreset &preset) { m_presets.set(preset); }

    QcNamedList<QcBiomeDefinition> m_biomes;
    QcNamedList<QcFeatureDefinition> m_features;
    QcNamedList<QcOreDefinition> m_ores;
    QcTerrainSettings m_terrain;
    QcNamedList<QcStructureDefinition> m_structures;
    QcShowcaseDefinition m_showcase;
    QcFlatDefinition m_flatWorld;
    QcVoidDefinition m_voidWorld;
    QcNamedList<QcWorldOptionGroup> m_optionGroups;
    QcNamedList<QcWorldOptionDefinition> m_options;
    QcNamedList<QcWorldPreset> m_presets;
};

inline void QcWorldgenCatalog::addWorldOptions(const QcWorldOptionSet &set)
{
    for (const QcWorldOptionGroup &group : set.groups) {
        m_optionGroups.set(group);
    }
    for (const QcWorldOptionDefinition &option : set.options) {
        setOption(option);
    }
    for (const QcWorldPreset &preset : set.presets) {
        setPreset(preset);
    }
}
