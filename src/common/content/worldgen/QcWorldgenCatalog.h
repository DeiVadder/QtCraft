// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcNamedList.h"
#include "common/content/worldgen/QcShowcaseDefinition.h"
#include "common/content/worldgen/QcStructureDefinition.h"
#include "common/content/worldgen/QcWorldgenDefinitions.h"
#include "common/content/worldgen/QcWorldOptionDefinitions.h"
#include "common/content/worldgen/QcWorldTypeDefinitions.h"

/*! \brief What the world generators build from (part of QcContentRegistry, server side only, not
    synchronised): biomes, features, ores and the terrain settings, the structures and the settings
    of the showcase, flat and void world types, and the options a world may set on them (decision
    E-917).

    Definitions are replaced by name, settings by a later pack. */
class QcWorldgenCatalog
{
public:
    /*! Adds the biome \a definition, or replaces the one of the same name in its place. */
    void registerBiome(QcBiomeDefinition definition) { m_biomes.set(std::move(definition)); }
    /*! Returns the biomes in registration order. */
    const QList<QcBiomeDefinition> &biomes() const { return m_biomes.items(); }
    /*! Removes the biome \a name, if there is one; the later ones move up. */
    void removeBiome(const QString &name) { m_biomes.remove(name); }
    /*! Returns the biome named \a name; null when there is none. */
    const QcBiomeDefinition *biome(const QString &name) const { return m_biomes.find(name); }
    /*! Adds the feature \a definition, or replaces the one of the same name in its place. */
    void registerFeature(QcFeatureDefinition definition) { m_features.set(std::move(definition)); }
    /*! Returns the features in registration order. */
    const QList<QcFeatureDefinition> &features() const { return m_features.items(); }
    /*! Adds the ore \a definition, or replaces the one of the same name in its place. */
    void registerOre(QcOreDefinition definition) { m_ores.set(std::move(definition)); }
    /*! Returns the ores in registration order. */
    const QList<QcOreDefinition> &ores() const { return m_ores.items(); }
    /*! Replaces the terrain settings with \a settings. */
    void setTerrainSettings(QcTerrainSettings settings) { m_terrain = std::move(settings); }
    /*! Returns the terrain settings. */
    const QcTerrainSettings &terrainSettings() const { return m_terrain; }

    /*! Adds \a structure, or replaces the one of the same name in its place. */
    void registerStructure(QcStructureDefinition structure) { m_structures.set(std::move(structure)); }
    /*! Returns the structures in registration order. */
    const QList<QcStructureDefinition> &structures() const { return m_structures.items(); }
    /*! Returns the structure named \a name; null when there is none. */
    const QcStructureDefinition *structure(const QString &name) const { return m_structures.find(name); }

    /*! Replaces the showcase settings with \a showcase. */
    void setShowcase(QcShowcaseDefinition showcase) { m_showcase = std::move(showcase); }
    /*! Returns the showcase settings. */
    const QcShowcaseDefinition &showcase() const { return m_showcase; }
    /*! Replaces the flat world settings with \a flat. */
    void setFlatWorld(QcFlatDefinition flat) { m_flatWorld = std::move(flat); }
    /*! Returns the flat world settings. */
    const QcFlatDefinition &flatWorld() const { return m_flatWorld; }
    /*! Replaces the void world settings with \a empty. */
    void setVoidWorld(QcVoidDefinition empty) { m_voidWorld = std::move(empty); }
    /*! Returns the void world settings. */
    const QcVoidDefinition &voidWorld() const { return m_voidWorld; }

    /*! Adds the groups, options and presets of \a set, one pack's world generation options
        (decision E-917); an entry with the name of an existing one replaces it in its place. Called
        in the order of the packs and their files. */
    void addWorldOptions(const QcWorldOptionSet &set);
    /*! Returns the option groups in registration order. */
    const QList<QcWorldOptionGroup> &worldOptionGroups() const { return m_optionGroups.items(); }
    /*! Returns the options in registration order. */
    const QList<QcWorldOptionDefinition> &worldOptions() const { return optionList().items(); }
    /*! Returns the option named \a name; null when there is none. */
    const QcWorldOptionDefinition *worldOption(const QString &name) const { return optionList().find(name); }
    /*! Returns the presets in registration order. */
    const QList<QcWorldPreset> &worldPresets() const { return presetList().items(); }
    /*! Returns the preset named \a name; null when there is none. */
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
