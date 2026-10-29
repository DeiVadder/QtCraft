// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcNamedList.h"
#include "common/content/villages/QcTradeDefinitions.h"
#include "common/content/villages/QcVillageDefinitions.h"

// Villages of the content (part of QcContentRegistry, server side only, not synchronised): village
// styles and settings (decision E-403), villager species and professions (decisions E-411, E-421)
// and the professions' offers (decision E-414). Definitions are replaced by name, the settings by a
// later pack.
class QcVillageCatalog
{
public:
    // Each registration counts QcVillageStyleDefinition::registered up.
    void registerStyle(QcVillageStyleDefinition style);
    const QList<QcVillageStyleDefinition> &styles() const { return m_styles.items(); }
    // Changes the style at `index` with `change(QcVillageStyleDefinition &)`; its name stays (QcNamedList::update).
    template<typename Change>
    void updateStyle(qsizetype index, Change change)
    {
        m_styles.update(index, change);
    }
    const QcVillageStyleDefinition *style(const QString &name) const { return m_styles.find(name); }
    void setSettings(QcVillageSettings settings) { m_settings = std::move(settings); }
    const QcVillageSettings &settings() const { return m_settings; }
    // The golems guarding villages of `style`: the style's golem fields over the settings' (decision
    // E-421).
    QcVillageGolemSettings golemOfStyle(const QString &style) const;

    void registerVillagerSettings(QcVillagerSettings settings) { m_villagerSettings.set(std::move(settings)); }
    const QList<QcVillagerSettings> &villagerSpecies() const { return m_villagerSettings.items(); }
    // Changes every species with `change(QcVillagerSettings &)`; the names stay (QcNamedList::update).
    template<typename Change>
    void updateVillagerSpecies(Change change)
    {
        m_villagerSettings.updateAll(change);
    }
    const QcVillagerSettings *villagerSettings(const QString &name) const { return m_villagerSettings.find(name); }
    [[nodiscard]] std::optional<qsizetype> villagerSettingsIndex(const QString &name) const { return m_villagerSettings.indexOf(name); }
    // Removes the species `name` (QcVillageContent::resolveSpecies drops a broken one).
    void removeVillagerSettings(const QString &name) { m_villagerSettings.remove(name); }

    void registerProfession(QcVillagerProfession profession) { m_professions.set(std::move(profession)); }
    const QList<QcVillagerProfession> &professions() const { return m_professions.items(); }
    const QcVillagerProfession *profession(const QString &name) const { return m_professions.find(name); }
    void registerTradeSet(QcTradeSet set) { m_tradeSets.set(std::move(set)); }
    const QList<QcTradeSet> &tradeSets() const { return m_tradeSets.items(); }

private:
    QcNamedList<QcVillageStyleDefinition> m_styles;
    QcVillageSettings m_settings;
    QcNamedList<QcVillagerSettings> m_villagerSettings;
    QcNamedList<QcVillagerProfession> m_professions;
    QcNamedList<QcTradeSet> m_tradeSets;
};
