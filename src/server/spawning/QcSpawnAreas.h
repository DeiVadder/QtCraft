// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcWorldConstants.h"
#include "server/mobs/QcMobExchange.h"
#include "server/spawning/QcMonsterCap.h"
#include "worldgen/generators/QcStructureSites.h"

#include <QList>
#include <QRandomGenerator>

#include <memory>

class QcServerWorld;

/*! \brief The spawn areas of structures around the living players (decision E-871, the original
    game's spawn overrides): in the footprint of a witch hut over the whole height of the world the
    natural monster spawns bring forth only witches, in the boxes of a fortress's parts only its
    kinds (decision E-1074), by the monsters' rules of light, ground, distance and cap
    (QcNaturalSpawner).

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

    /*! \brief A monster kind of an area with its weight and the rule it spawns by there.
        \qcfields */
    struct Kind
    {
        QcCreatureKindId type;
        int weight = 1;
        QcMobDefinition::SpawnRule rule;
    };

    /*! \brief An area with its kinds; kinds that are no monsters are left out. */
    struct Area
    {
        /*! The area. */
        QcSpawnArea site;
        /*! Its monster kinds. */
        QList<Kind> kinds;

        /*! Returns the sum of the kinds' weights. */
        [[nodiscard]] int totalWeight() const;
        /*! Returns the kind a draw \a draw from 0 to totalWeight() - 1 falls on. */
        [[nodiscard]] const Kind &kindAt(int draw) const;
    };

    explicit QcSpawnAreas(std::shared_ptr<const QcContentRegistry> content);

    /*! Returns the rule a monster of \a definition spawns by in an area as \a monster: its main
        rule without biomes, time, chunk share, height limit, sky or water condition and with
        weight 1; its ground stays, and its light test unless \a monster gives a block light
        limit of its own (then the limit alone, as in the original game's nether). */
    static QcMobDefinition::SpawnRule ruleOf(const QcMobDefinition &definition, const QcStructureDefinition::SpawnArea::Monster &monster);

    /*! Called once a tick by the monster pass: every refreshTicks calls the areas of \a world
        around \a players. */
    void refresh(QcServerWorld &world, const QList<QcMobExchange::PlayerView> &players);
    /*! Returns the area that holds the cell \a feet, if any. */
    [[nodiscard]] const Area *at(QcBlockPos feet) const;
    /*! Returns whether a part of an area holds a cell of the column at \a x, \a z. */
    [[nodiscard]] bool touchesColumn(int x, int z) const;
    /*! Returns the places among \a floors, the floors of a column an area touches (each with
        \c {x}, \c {floorY} and \c {z}), with draws from \a draws: every floor inside an area
        is a place of its own (decision E-871: each storey of a farm gets the rate of a single
        floor, as with the original game's random height), from a random one on, so the first
        ones do not take the room of the cap first; of the floors outside the areas (under a
        fortress's bridge, decision E-1074) one, as in any other column. */
    template <typename Place>
    [[nodiscard]] QList<Place> placesIn(const QList<Place> &floors, QRandomGenerator &draws) const
    {
        QList<Place> inside;
        QList<Place> outside;
        for (const Place &place : floors) {
            (at({place.x, place.floorY + 1, place.z}) ? inside : outside).append(place);
        }
        QList<Place> places;
        const qsizetype first = inside.isEmpty() ? 0 : draws.bounded(static_cast<int>(inside.size()));
        for (qsizetype i = 0; i < inside.size(); ++i) {
            places.append(inside[(first + i) % inside.size()]);
        }
        if (!outside.isEmpty()) {
            places.append(outside[static_cast<qsizetype>(draws.bounded(static_cast<int>(outside.size())))]);
        }
        return places;
    }
    /*! Returns every area known. */
    const QList<Area> &areas() const { return m_areas; }

private:
    [[nodiscard]] const QcContentRegistry &content() const { return *m_content; }
    void setAreas(QList<Area> areas) { m_areas = std::move(areas); }
    [[nodiscard]] Area resolve(const QcSpawnArea &site) const;
    // The areas of `sites` with their kinds, each once.
    [[nodiscard]] QList<Area> resolveAll(const QList<QcSpawnArea> &sites) const;
    // The current areas within `reach` of `from` (for a player whose regions are not planned).
    [[nodiscard]] QList<QcSpawnArea> keptNear(QcBlockPos from) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    QList<Area> m_areas;
    quint64 m_ticks = 0;
};
