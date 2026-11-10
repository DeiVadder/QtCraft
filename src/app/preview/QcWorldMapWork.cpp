// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/preview/QcWorldMapWork.h"

#include "app/debug/QcAppLogging.h"
#include "app/preview/QcWorldMapPainter.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcDowncast.h"
#include "worldgen/generators/QcDimensionMaps.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/generators/QcWorldOptions.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// The code of a cell in QcWorldMapResult::cells.
qint16 codeOf(QcBiomeMap::Cell cell)
{
    if (cell.water == QcBiomeMap::Water::Sea) {
        return cell.frozen ? QcWorldMapResult::iceCell : QcWorldMapResult::seaCell;
    }
    return cell.biome >= 0 ? static_cast<qint16>(cell.biome) : QcWorldMapResult::unknownCell;
}

// The drawn map of one pass with its counts; `seaLevel` is the height the painter takes for it.
QcWorldMapResult drawn(const QcBiomeMap &map, std::optional<int> seaLevel, const QList<quint32> &colours)
{
    QcWorldMapResult result;
    result.area = map.area();
    result.biomeColours = colours;
    result.image = QcWorldMapPainter::paint(map, colours, seaLevel);
    result.biomeCells = map.biomeCells(static_cast<int>(colours.size()));
    result.cells.reserve(static_cast<qsizetype>(map.size()) * map.size());
    for (int z = 0; z < map.size(); ++z) {
        for (int x = 0; x < map.size(); ++x) {
            const QcBiomeMap::Cell &cell = map.cell(x, z);
            if (cell.water == QcBiomeMap::Water::Sea) {
                ++(cell.frozen ? result.iceCells : result.seaCells);
            }
            result.cells.append(codeOf(cell));
        }
    }
    return result;
}

// The content a world created with `request` generates with: every option, the form's values
// over the defaults, set into a copy of the content (decision E-917); null when they are refused.
std::shared_ptr<const QcContentRegistry> appliedContent(const QcWorldMapRequest &request)
{
    const QString terrain = QString::fromLatin1(QcWorldGenerators::terrain);
    const QcResult<QJsonObject> stored = QcWorldOptions::valuesToStore(terrain, request.values, *request.content);
    if (!stored) {
        qCWarning(lcPreview, "Map preview: options refused: %s", qPrintable(stored.error()));
        return nullptr;
    }
    return QcWorldOptions::apply(request.content, terrain, *stored).content;
}

// How a portal lit in the overworld of `content` leads to `dimension`: by the scales if one does.
QcPortalDefinition::Arrival arrivalFromOverworld(const QcContentRegistry &content, const QcDimensionDefinition &dimension)
{
    using Arrival = QcPortalDefinition::Arrival;
    const QString overworld = QString::fromLatin1(QcDimensionDefinition::overworldName);
    Arrival best = Arrival::None;
    for (const QcPortalDefinition &portal : content.worldgen().portals()) {
        const Arrival arrival = portal.arrivalIn(dimension, overworld);
        if (arrival == Arrival::Scaled) {
            return arrival;
        }
        best = arrival == Arrival::Fixed ? arrival : best;
    }
    return best;
}

} // namespace

std::shared_ptr<const QcTerrainGenerator> QcWorldMapWorker::generatorFor(const QcWorldMapRequest &request)
{
    // Exactly what "Create and play" stores and the world then generates with.
    const std::shared_ptr<const QcContentRegistry> applied = appliedContent(request);
    if (!applied) {
        return nullptr;
    }
    // The world's own factory, as the version the world will have (decision E-922); a terrain world
    // gets a QcTerrainGenerator.
    const QString terrain = QString::fromLatin1(QcWorldGenerators::terrain);
    const std::shared_ptr<const QcWorldGenerator> created = QcWorldGenerators::create(terrain, applied, request.seed, {}, request.generatorVersion);
    if (!created) {
        qCWarning(lcPreview, "Map preview: no world generator version %d", request.generatorVersion);
        return nullptr;
    }
    return std::shared_ptr<const QcTerrainGenerator>(created, qcDowncast<const QcTerrainGenerator>(created.get()));
}

std::shared_ptr<const QcWorldGenerator> QcWorldMapWorker::dimensionGeneratorFor(const QcWorldMapRequest &request)
{
    if (request.showsOverworld()) {
        return generatorFor(request);
    }
    const std::shared_ptr<const QcContentRegistry> applied = appliedContent(request);
    if (!applied) {
        return nullptr;
    }
    const QcDimensionDefinition *dimension = applied->worldgen().dimension(request.dimension);
    if (!dimension) {
        qCWarning(lcPreview, "Map preview: the content has no dimension %s", qPrintable(request.dimension));
        return nullptr;
    }
    const int version = QcWorldGenerators::versionForNewWorld(*dimension);
    std::shared_ptr<const QcWorldGenerator> created = QcWorldGenerators::createDimension(*dimension, applied, request.seed, version);
    if (!created) {
        qCWarning(lcPreview, "Map preview: no generator %s version %d", qPrintable(dimension->generatorType), version);
    }
    return created;
}

QcBiomeMap::Area QcWorldMapWorker::withCells(QcBiomeMap::Area area, int cells)
{
    area.blocksPerCell = std::max(1, 2 * area.radius / std::max(1, cells));
    return area;
}

QcBiomeMap::Area QcWorldMapWorker::sitesArea(QcBiomeMap::Area area)
{
    area.radius = std::min(area.radius, sitesRadius);
    area.blocksPerCell = 1;
    return area;
}

std::shared_ptr<const QcWorldGenerator> QcWorldMapWorker::generator(const QcWorldMapRequest &request)
{
    if (!m_generator || !world().sameWorld(request) || world().dimension != request.dimension) {
        // The old one goes first: its plan caches do not stay while the new one is set up.
        m_generator.reset();
        m_generator = dimensionGeneratorFor(request);
        setWorld(request);
    }
    return m_generator;
}

std::optional<QPoint> QcWorldMapWorker::worldSpawn(const QcWorldMapRequest &request, const QcWorldGenerator &shown)
{
    if (!m_spawn || !m_spawnWorld.sameWorld(request)) {
        // The server's own search (QcSpawnPoint::column), on the overworld's generator.
        const std::shared_ptr<const QcTerrainGenerator> overworld = request.showsOverworld() ? nullptr : generatorFor(request);
        if (!request.showsOverworld() && !overworld) {
            return std::nullopt;
        }
        const QcBlockPos spawn = overworld ? overworld->spawnPoint() : shown.spawnPoint();
        m_spawn = QPoint(spawn.x, spawn.z);
        m_spawnWorld = request;
    }
    return m_spawn;
}

std::optional<QcWorldMapSpawn> QcWorldMapWorker::spawnIn(const QcWorldMapRequest &request, const QcWorldGenerator &shown)
{
    const QcDimensionDefinition *dimension = QcDimensionMaps::dimensionOf(*request.content, request.dimension);
    if (!dimension) {
        return std::nullopt;
    }
    using Arrival = QcPortalDefinition::Arrival;
    const Arrival arrival = request.showsOverworld() ? Arrival::Scaled : arrivalFromOverworld(*request.content, *dimension);
    if (arrival == Arrival::None) {
        return std::nullopt;
    }
    if (arrival == Arrival::Fixed && dimension->arrival) {
        const QcBlockPos point = dimension->arrival->position;
        return QcWorldMapSpawn{QPoint(point.x, point.z), QcWorldMapSpawn::Kind::FixedArrival};
    }

    const std::optional<QPoint> spawn = worldSpawn(request, shown);
    if (!spawn) {
        return std::nullopt;
    }
    if (request.showsOverworld()) {
        return QcWorldMapSpawn{*spawn, QcWorldMapSpawn::Kind::WorldSpawn};
    }
    // The point a scaled link takes (QcPortalLinks::scaled).
    const double ratio = request.content->worldgen().overworld().coordinateScale / dimension->coordinateScale;
    const QPoint scaled(static_cast<int>(std::floor(spawn->x() * ratio)), static_cast<int>(std::floor(spawn->y() * ratio)));
    return QcWorldMapSpawn{scaled, QcWorldMapSpawn::Kind::ScaledPortal};
}

void QcWorldMapWorker::forget()
{
    m_generator.reset();
    setWorld({});
    m_spawn.reset();
    m_spawnWorld = {};
}

void QcWorldMapWorker::run(const QcWorldMapRequest &request, quint64 build, const QcBiomeMap::Cancelled &cancelled, const Deliver &deliver)
{
    QElapsedTimer timer;
    timer.start();
    const std::shared_ptr<const QcWorldGenerator> made = generator(request);
    if (!made || cancelled()) {
        return;
    }
    const QcDimensionDefinition *dimension = QcDimensionMaps::dimensionOf(*request.content, request.dimension);
    const QcDimensionMaps::Sampler sampler = dimension ? QcDimensionMaps::samplerOf(*dimension, *made, *request.content) : QcDimensionMaps::Sampler{};
    if (!sampler.map) {
        return;
    }
    const QList<quint32> colours = QcWorldMapPainter::biomeColours(*request.content);

    // Only the sites: the map of this world and view is shown already.
    const auto passes = request.passes == QcWorldMapRequest::Passes::All
                            ? std::vector{std::pair{QcWorldMapResult::Pass::Coarse, coarseCells}, std::pair{QcWorldMapResult::Pass::Fine, fineCells}}
                            : std::vector<std::pair<QcWorldMapResult::Pass, int>>{};
    for (const auto &[pass, cells] : passes) {
        const std::optional<QcBiomeMap> map = sampler.map(withCells(request.area, cells), cancelled);
        if (!map) {
            return;
        }
        QcWorldMapResult result = drawn(*map, sampler.seaLevel, colours);
        result.build = build;
        result.pass = pass;
        result.milliseconds = timer.restart();
        deliver(std::move(result));
    }

    // After the fine map, which does not wait for the search; a change made by now needs none.
    if (cancelled()) {
        return;
    }
    if (request.passes == QcWorldMapRequest::Passes::All) {
        if (const std::optional<QcWorldMapSpawn> spawn = spawnIn(request, *made)) {
            QcWorldMapResult result;
            result.build = build;
            result.pass = QcWorldMapResult::Pass::Spawn;
            result.spawn = *spawn;
            deliver(std::move(result));
        }
        timer.restart();
    }

    if (!request.sites) {
        return;
    }
    std::optional<QList<QcBiomeMap::Site>> sites = sampler.sites(sitesArea(request.area), cancelled);
    if (!sites) {
        return;
    }
    QcWorldMapResult result;
    result.build = build;
    result.pass = QcWorldMapResult::Pass::Sites;
    result.area = sitesArea(request.area);
    result.sites = std::move(*sites);
    result.siteKinds = sampler.kinds;
    result.milliseconds = timer.restart();
    deliver(std::move(result));
}
