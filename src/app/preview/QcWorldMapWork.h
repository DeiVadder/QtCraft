// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/world/QcGeneratorVersion.h"
#include "worldgen/generators/QcBiomeMap.h"

#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QPoint>

#include <functional>
#include <memory>
#include <optional>

class QcContentRegistry;
class QcTerrainGenerator;
class QcWorldGenerator;

// What the map preview of a new world is computed from (decision E-921): the menu's content, the
// world generation options as the form holds them, the seed, the generator version the world will
// have (decision E-922; the current one while the menu offers no choice), the square shown, whether villages
// and structures are wanted and whether the map itself is (not when only the sites of the map
// already shown are asked for), and the dimension shown (decision E-1077).
struct QcWorldMapRequest
{
    enum class Passes : quint8 { All, SitesOnly };

    std::shared_ptr<const QcContentRegistry> content;
    QJsonObject values;
    qint64 seed = 0;
    QcBiomeMap::Area area;
    bool sites = false;
    Passes passes = Passes::All;
    int generatorVersion = QcGeneratorVersion::current;
    // The name of a dimension of the content that has a map (QcDimensionMaps::hasMap).
    QString dimension = QString::fromLatin1(QcDimensionDefinition::overworldName);

    bool operator==(const QcWorldMapRequest &) const = default;
    // Whether `other` describes the same world, whichever dimension of it they show.
    [[nodiscard]] bool sameWorld(const QcWorldMapRequest &other) const
    {
        return content == other.content && values == other.values && seed == other.seed && generatorVersion == other.generatorVersion;
    }
    // Whether the dimension shown is the world's overworld.
    [[nodiscard]] bool showsOverworld() const { return dimension == QLatin1StringView(QcDimensionDefinition::overworldName); }
};

// The marker of the spawn on a map (decision E-1079): the column in the blocks of the dimension
// shown and what stands there: the world spawn in the overworld, elsewhere where a portal at the
// world spawn comes out, by the scales or at the dimension's fixed arrival point.
struct QcWorldMapSpawn
{
    enum class Kind : quint8 { WorldSpawn, ScaledPortal, FixedArrival };

    QPoint column;
    Kind kind = Kind::WorldSpawn;

    bool operator==(const QcWorldMapSpawn &) const = default;
};

// One step of a preview: the coarse map, the refined one, the spawn (a dimension no portal of the
// overworld leads to has none), then (when wanted) the sites, then the end.
struct QcWorldMapResult
{
    // Spawn comes before Sites but is no pass of the map's: it neither counts in the progress nor
    // has a time of its own. Done ends a build that was not cancelled (also one with nothing to show).
    enum class Pass : quint8 { Coarse, Fine, Sites, Spawn, Done };

    quint64 build = 0;
    Pass pass = Pass::Coarse;
    QcBiomeMap::Area area;
    // The drawn map, one pixel per cell (Coarse and Fine).
    QImage image;
    // What covers each cell, row by row (Coarse and Fine): its biome, or seaCell / iceCell; the GUI
    // thread names the cell under the pointer from it.
    static constexpr qint16 seaCell = -1;
    static constexpr qint16 iceCell = -2;
    // A biome the content does not have (named by no legend).
    static constexpr qint16 unknownCell = -3;
    QList<qint16> cells;
    // Cells per biome (Coarse and Fine), cells of open sea and lakes and of frozen ones.
    QList<int> biomeCells;
    int seaCells = 0;
    int iceCells = 0;
    // The colour of each biome on the map, 0xRRGGBB.
    QList<quint32> biomeColours;
    QList<QcBiomeMap::Site> sites;
    // The spawn marker (Spawn).
    QcWorldMapSpawn spawn;
    // Every kind of site the generator can place, in the order of QcBiomeMap::siteKinds (Sites).
    QStringList siteKinds;
    // How long the step took, in ms (the generator's set-up included in the first step).
    qint64 milliseconds = 0;
};

// The worker side of the map preview: builds the generator exactly as a world created from the
// form gets it (QcWorldOptions::valuesToStore, QcWorldOptions::apply, QcTerrainGenerator with the
// seed; for another dimension the generator it gets, decision E-1077) and samples it in passes. Lives on the preview's single worker thread; keeps the last
// generator, so moving or zooming the view of the same world does not set it up again.
class QcWorldMapWorker
{
public:
    // Cells along a side of the coarse and the fine pass.
    static constexpr int coarseCells = 64;
    static constexpr int fineCells = 256;
    // Villages and structures are looked for at most this far from the view's centre (half the
    // side of a square): planning their regions over 4096 × 4096 blocks takes about 3.7 s in a
    // debug build, over 8192 × 8192 about 12 s and overflows the plan caches (decision E-921).
    static constexpr int sitesRadius = 1024;

    using Deliver = std::function<void(QcWorldMapResult)>;

    // Runs the passes of `request` (`build`), handing each result to `deliver`; stops as soon as
    // `cancelled` says so.
    void run(const QcWorldMapRequest &request, quint64 build, const QcBiomeMap::Cancelled &cancelled, const Deliver &deliver);
    // Lets go of the last generator and the content it was made from (the preview was hidden or
    // released): the menu's content, its copy with the options set and the generator's plan caches.
    void forget();

    // The generator a world created with `request`'s content, values and seed gets; null when the
    // values are refused.
    [[nodiscard]] static std::shared_ptr<const QcTerrainGenerator> generatorFor(const QcWorldMapRequest &request);
    // The generator of `request`'s dimension of that world: the world's for the overworld, else
    // the one the server opens for a new world (QcWorldGenerators::createDimension and
    // versionForNewWorld); null when the values are refused or the content has no such dimension.
    [[nodiscard]] static std::shared_ptr<const QcWorldGenerator> dimensionGeneratorFor(const QcWorldMapRequest &request);
    // `area` with as many blocks per cell as `cells` along a side need (at least 1).
    [[nodiscard]] static QcBiomeMap::Area withCells(QcBiomeMap::Area area, int cells);
    // The part of `area` searched for villages and structures (at most sitesRadius around its centre).
    [[nodiscard]] static QcBiomeMap::Area sitesArea(QcBiomeMap::Area area);

private:
    // The generator of `request`'s world and dimension.
    [[nodiscard]] std::shared_ptr<const QcWorldGenerator> generator(const QcWorldMapRequest &request);
    // The spawn marker of `request`'s dimension, whose generator is `shown` (decision E-1079); none
    // when no portal of the overworld leads there.
    [[nodiscard]] std::optional<QcWorldMapSpawn> spawnIn(const QcWorldMapRequest &request, const QcWorldGenerator &shown);
    // The world spawn's column: the search of the overworld's generator, once per world.
    [[nodiscard]] std::optional<QPoint> worldSpawn(const QcWorldMapRequest &request, const QcWorldGenerator &shown);
    // The request the cached generator was made for.
    const QcWorldMapRequest &world() const { return m_world; }
    void setWorld(QcWorldMapRequest world) { m_world = std::move(world); }

    QcWorldMapRequest m_world;
    std::shared_ptr<const QcWorldGenerator> m_generator; // NOLINT(qc-member-access): the cache of generator(), used only there and in forget()
    // The overworld's spawn column of the world of m_spawnWorld.
    std::optional<QPoint> m_spawn; // NOLINT(qc-member-access): the cache of worldSpawn(), used only there and in forget()
    QcWorldMapRequest m_spawnWorld; // NOLINT(qc-member-access): the key of that cache
};
