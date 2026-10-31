// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcWorldConstants.h"
#include "server/mobs/QcMobExchange.h"
#include "server/spawning/QcMonsterCap.h"
#include "worldgen/generators/QcStructureSites.h"

#include <QList>

#include <memory>

class QcServerWorld;

/*! \brief The spawn areas of structures around the living players (decision E-871, the original
    game's spawn overrides): in the footprint of a witch hut over the whole height of the world the
    natural monster spawns bring forth only witches, by the monsters' rules of light, ground,
    distance and cap (QcNaturalSpawner).

    Every refreshTicks calls it asks the world generator for the areas within the monster areas
    of the players (QcWorldGenerator::plannedStructureSitesNear, from its plans; around a player
    whose regions are not all planned yet they are planned on a generation thread, the areas it
    had stay meanwhile, and the new ones come with a later refresh). Nothing is saved. */
class QcSpawnAreas
{
public:
    /*! Ticks between two refreshes. */
    static constexpr int refreshTicks = 20;
    /*! How far from a player areas are looked for: the edge of its monster area (QcMonsterCap). */
    static constexpr int reach = (QcMonsterCap::reach + 1) * QcWorld::chunkSize;

    /*! \brief An area with its kinds as indices; kinds that are no monsters are left out. */
    struct Area
    {
        /*! The area. */
        QcSpawnArea site;
        /*! Its monster kinds (indices). */
        QList<int> types;
    };

    explicit QcSpawnAreas(std::shared_ptr<const QcContentRegistry> content);

    /*! Returns the rule a monster of \a definition spawns by in an area: its main rule without
        biomes, time, chunk share, height limit, sky or water condition and with weight 1; its
        light test and ground stay. */
    static QcMobDefinition::SpawnRule ruleOf(const QcMobDefinition &definition);

    /*! Called once a tick by the monster pass: every refreshTicks calls the areas of \a world
        around \a players. */
    void refresh(QcServerWorld &world, const QList<QcMobExchange::PlayerView> &players);
    /*! Returns the area whose footprint holds the column at \a x, \a z, if any. */
    [[nodiscard]] const Area *at(int x, int z) const;
    /*! Returns every area known. */
    const QList<Area> &areas() const { return m_areas; }

private:
    [[nodiscard]] Area resolve(const QcSpawnArea &site) const;
    // The areas of `sites` with their kinds, each once.
    [[nodiscard]] QList<Area> resolveAll(const QList<QcSpawnArea> &sites) const;
    // The current areas within `reach` of `from` (for a player whose regions are not planned).
    [[nodiscard]] QList<QcSpawnArea> keptNear(QcBlockPos from) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    QList<Area> m_areas;
    quint64 m_ticks = 0;
};
