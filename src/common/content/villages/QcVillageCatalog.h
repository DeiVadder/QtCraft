// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcNamedList.h"
#include "common/content/villages/QcTradeDefinitions.h"
#include "common/content/villages/QcVillageDefinitions.h"

/*! \brief Villages of the content (part of QcContentRegistry, server side only, not synchronised):
    village styles and settings (decision E-403), villager species and professions (decisions E-411,
    E-421) and the professions' offers (decision E-414).

    Definitions are replaced by name, the settings by a later pack. */
class QcVillageCatalog
{
public:
    /*! Adds \a style, or replaces the style of the same name in its place; each registration
        counts QcVillageStyleDefinition::registered up. */
    void registerStyle(QcVillageStyleDefinition style);
    /*! Returns the village styles in registration order. */
    const QList<QcVillageStyleDefinition> &styles() const { return m_styles.items(); }
    /*! Changes the style at \a index with \a change, called as
        \c {change(QcVillageStyleDefinition &)}; its name stays (QcNamedList::update). */
    template<typename Change>
    void updateStyle(qsizetype index, Change change)
    {
        m_styles.update(index, change);
    }
    /*! Returns the style \a name, or \c {nullptr} if there is none. */
    const QcVillageStyleDefinition *style(const QString &name) const { return m_styles.find(name); }
    /*! Replaces the village settings with \a settings. */
    void setSettings(QcVillageSettings settings) { m_settings = std::move(settings); }
    /*! Returns the village settings. */
    const QcVillageSettings &settings() const { return m_settings; }
    /*! Returns the golems guarding villages of \a style: the style's golem fields over the
        settings' (decision E-421). */
    QcVillageGolemSettings golemOfStyle(const QString &style) const;

    /*! Adds the villager species \a settings, or replaces the one of the same name in its
        place. */
    void registerVillagerSettings(QcVillagerSettings settings) { m_villagerSettings.set(std::move(settings)); }
    /*! Returns the villager species in registration order. */
    const QList<QcVillagerSettings> &villagerSpecies() const { return m_villagerSettings.items(); }
    /*! Changes every species with \a change, called as \c {change(QcVillagerSettings &)}; the
        names stay (QcNamedList::update). */
    template<typename Change>
    void updateVillagerSpecies(Change change)
    {
        m_villagerSettings.updateAll(change);
    }
    /*! Returns the villager species \a name, or \c {nullptr} if there is none. */
    const QcVillagerSettings *villagerSettings(const QString &name) const { return m_villagerSettings.find(name); }
    /*! Returns the position of the species \a name in villagerSpecies(); none if there is none. */
    [[nodiscard]] std::optional<qsizetype> villagerSettingsIndex(const QString &name) const { return m_villagerSettings.indexOf(name); }
    /*! Removes the species \a name (QcVillageContent::resolveSpecies drops a broken one). */
    void removeVillagerSettings(const QString &name) { m_villagerSettings.remove(name); }

    /*! Adds \a profession, or replaces the one of the same name in its place. */
    void registerProfession(QcVillagerProfession profession) { m_professions.set(std::move(profession)); }
    /*! Returns the professions in registration order. */
    const QList<QcVillagerProfession> &professions() const { return m_professions.items(); }
    /*! Returns the profession \a name, or \c {nullptr} if there is none. */
    const QcVillagerProfession *profession(const QString &name) const { return m_professions.find(name); }
    /*! Adds the set of offers \a set, or replaces the one of the same name in its place. */
    void registerTradeSet(QcTradeSet set) { m_tradeSets.set(std::move(set)); }
    /*! Returns the sets of offers in registration order. */
    const QList<QcTradeSet> &tradeSets() const { return m_tradeSets.items(); }

private:
    QcNamedList<QcVillageStyleDefinition> m_styles;
    QcVillageSettings m_settings;
    QcNamedList<QcVillagerSettings> m_villagerSettings;
    QcNamedList<QcVillagerProfession> m_professions;
    QcNamedList<QcTradeSet> m_tradeSets;
};
