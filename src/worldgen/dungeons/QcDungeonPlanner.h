// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcDungeonDefinition.h"
#include "common/content/registry/QcContentRegistry.h"
#include "worldgen/dungeons/QcDungeonPlan.h"
#include "worldgen/terrain/QcTerrainColumn.h"

#include <QHash>
#include <QSet>

#include <array>
#include <memory>
#include <optional>

class QcTerrainGenerator;

// Plans the dungeon of a region (decision E-522) from the seed and the terrain noise: whether the
// region has one (`chance`), a place for its room deep below the ground where caves take little of
// it, the spawner's kind by weight, the chests against the walls, and the corridors to caves within
// `reach` (one per side at most, straight or with one bend, the shortest first, at most
// `corridors`), with their lining and lights. Thread-safe: plan() only reads immutable state.
class QcDungeonPlanner
{
public:
    // The blocks a plan places, resolved once; chests and lights by the side they face or hang on
    // (north, east, south, west).
    struct Blocks
    {
        QcBlockId wall = qcAirBlockId;
        QcBlockId moss = qcAirBlockId;
        QcBlockId spawner = qcAirBlockId;
        std::array<QcBlockId, 4> chests{};
        std::array<QcBlockId, 4> lights{};
    };

    // Unresolvable blocks switch dungeons off with a warning (warnings()).
    QcDungeonPlanner(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed);

    const QStringList &warnings() const { return m_warnings; }

    bool enabled() const { return m_enabled; }
    const QcDungeonSettings &settings() const { return m_settings; }
    const Blocks &blocks() const { return m_blocks; }
    // How far beyond its region a dungeon may reach (room, corridors and lining).
    int reach() const { return m_settings.maxSize + m_settings.maxLength + 2; }
    // The region's dungeon; null without one.
    [[nodiscard]] std::shared_ptr<const QcDungeonPlan> plan(int regionX, int regionZ) const;

private:
    // The terrain at cells, with the columns looked up once.
    class Probe;

    [[nodiscard]] std::optional<QcBlockPos> findRoom(Probe &probe, int regionX, int regionZ, int sizeX, int sizeZ) const;
    // At least `groundCover` blocks of ground above a room at `origin` (corners and middle).
    static bool deepEnough(Probe &probe, QcBlockPos origin, int sizeX, int sizeZ);
    // Whether caves take at most `limit` cells of a room at `origin` (counting stops beyond it).
    static bool fewCaves(Probe &probe, QcBlockPos origin, int sizeX, int sizeZ, double limit);
    // The spawner's creature, by the weights of the settings.
    [[nodiscard]] QString mobOf(const QcDungeonPlan &plan) const;
    // The chests of the room, drawn from chestPlaces().
    [[nodiscard]] QList<QcDungeonPlan::Placed> chestsOf(const QcDungeonPlan &plan) const;
    // Every place a chest may stand, with the chest facing into the room.
    QList<QcDungeonPlan::Placed> chestPlaces(const QcDungeonPlan &plan) const;
    // The shortest corridors from the room's doors to caves, at most `corridors` of them.
    [[nodiscard]] QList<QcDungeonPlan::Corridor> corridorsOf(Probe &probe, const QcDungeonPlan &plan) const;
    // The shortest way from a door outwards along (dx, dz) to a cave: straight, or with one bend.
    [[nodiscard]] std::optional<QcDungeonPlan::Corridor> corridorFrom(Probe &probe, QcBlockPos door, int dx, int dz) const;
    // Steps from `from` along `step` to the first open cell, at most `steps`; none when the way
    // comes too near the ground first.
    [[nodiscard]] static std::optional<int> firstOpen(Probe &probe, QcBlockPos from, std::pair<int, int> step, int steps);
    // The cells next to the corridor cell `index`.
    static QList<QcBlockPos> around(const QcDungeonPlan::Corridor &corridor, qsizetype index);
    // The cells next to the corridors outside the room and the corridors (`inside`), sorted.
    [[nodiscard]] static QList<QcBlockPos> liningOf(const QcDungeonPlan &plan, const QSet<QcBlockPos> &inside);
    // The lights of the corridors; `inside` holds the corridors' cells.
    [[nodiscard]] QList<QcDungeonPlan::Placed> lightsOf(const QcDungeonPlan &plan, const QSet<QcBlockPos> &inside) const;
    // A light on a wall beside the corridor's head cell `head` entered by `step`.
    [[nodiscard]] std::optional<QcDungeonPlan::Placed> lightAt(const QcDungeonPlan &plan, QcBlockPos head, std::pair<int, int> step,
                                                               const QSet<QcBlockPos> &inside) const;
    // A random value in [0, 1) of the plan's region and a purpose.
    double random(const QcDungeonPlan &plan, int purpose, int index = 0) const;

    const QcTerrainGenerator &m_terrain;
    QcDungeonSettings m_settings;
    QStringList m_warnings;
    quint64 m_seed = 0;
    bool m_enabled = false;
    Blocks m_blocks;
    // Half sizes of the spawner's spawn area.
    int m_areaX = 4;
    int m_areaY = 1;
    int m_areaZ = 4;
};
