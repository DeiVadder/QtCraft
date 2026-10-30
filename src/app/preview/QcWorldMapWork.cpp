// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/preview/QcWorldMapWork.h"

#include "app/debug/QcAppLogging.h"
#include "app/preview/QcWorldMapPainter.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcDowncast.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/generators/QcWorldOptions.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QElapsedTimer>

#include <algorithm>
#include <vector>

namespace {

// The drawn map of one pass with its counts.
QcWorldMapResult drawn(const QcBiomeMap &map, const QcTerrainGenerator &generator, const QList<quint32> &colours)
{
    QcWorldMapResult result;
    result.area = map.area();
    result.biomeColours = colours;
    result.image = QcWorldMapPainter::paint(map, colours, generator.seaLevel());
    result.biomeCells = map.biomeCells(static_cast<int>(colours.size()));
    result.cells.reserve(static_cast<qsizetype>(map.size()) * map.size());
    for (int z = 0; z < map.size(); ++z) {
        for (int x = 0; x < map.size(); ++x) {
            const QcBiomeMap::Cell &cell = map.cell(x, z);
            const bool sea = cell.water == QcBiomeMap::Water::Sea;
            if (sea) {
                ++(cell.frozen ? result.iceCells : result.seaCells);
            }
            result.cells.append(!sea ? static_cast<qint16>(cell.biome) : cell.frozen ? QcWorldMapResult::iceCell : QcWorldMapResult::seaCell);
        }
    }
    return result;
}

} // namespace

std::shared_ptr<const QcTerrainGenerator> QcWorldMapWorker::generatorFor(const QcWorldMapRequest &request)
{
    // Exactly what "Create and play" stores and the world then generates with (decision E-917):
    // every option, the form's values over the defaults, set into a copy of the content.
    const QString terrain = QString::fromLatin1(QcWorldGenerators::terrain);
    const QcResult<QJsonObject> stored = QcWorldOptions::valuesToStore(terrain, request.values, *request.content);
    if (!stored) {
        qCWarning(lcPreview, "Map preview: options refused: %s", qPrintable(stored.error()));
        return nullptr;
    }
    const QcWorldOptions::Applied applied = QcWorldOptions::apply(request.content, terrain, *stored);
    // The world's own factory, as the version the world will have (decision E-922); a terrain world
    // gets a QcTerrainGenerator.
    const std::shared_ptr<const QcWorldGenerator> created = QcWorldGenerators::create(terrain, applied.content, request.seed, {}, request.generatorVersion);
    if (!created) {
        qCWarning(lcPreview, "Map preview: no world generator version %d", request.generatorVersion);
        return nullptr;
    }
    return std::shared_ptr<const QcTerrainGenerator>(created, qcDowncast<const QcTerrainGenerator>(created.get()));
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

std::shared_ptr<const QcTerrainGenerator> QcWorldMapWorker::generator(const QcWorldMapRequest &request)
{
    if (!m_generator || !world().sameWorld(request)) {
        m_generator = generatorFor(request);
        setWorld(request);
    }
    return m_generator;
}

void QcWorldMapWorker::forget()
{
    m_generator.reset();
    setWorld({});
}

void QcWorldMapWorker::run(const QcWorldMapRequest &request, quint64 build, const QcBiomeMap::Cancelled &cancelled, const Deliver &deliver)
{
    QElapsedTimer timer;
    timer.start();
    const std::shared_ptr<const QcTerrainGenerator> terrain = generator(request);
    if (!terrain || cancelled()) {
        return;
    }
    const QList<quint32> colours = QcWorldMapPainter::biomeColours(*request.content);

    // Only the sites: the map of this world and view is shown already.
    const auto passes = request.passes == QcWorldMapRequest::Passes::All
                            ? std::vector{std::pair{QcWorldMapResult::Pass::Coarse, coarseCells}, std::pair{QcWorldMapResult::Pass::Fine, fineCells}}
                            : std::vector<std::pair<QcWorldMapResult::Pass, int>>{};
    for (const auto &[pass, cells] : passes) {
        const std::optional<QcBiomeMap> map = QcBiomeMap::sample(*terrain, withCells(request.area, cells), cancelled);
        if (!map) {
            return;
        }
        QcWorldMapResult result = drawn(*map, *terrain, colours);
        result.build = build;
        result.pass = pass;
        result.milliseconds = timer.restart();
        deliver(std::move(result));
    }

    if (!request.sites) {
        return;
    }
    std::optional<QList<QcBiomeMap::Site>> sites = QcBiomeMap::sites(*terrain, sitesArea(request.area), cancelled);
    if (!sites) {
        return;
    }
    QcWorldMapResult result;
    result.build = build;
    result.pass = QcWorldMapResult::Pass::Sites;
    result.area = sitesArea(request.area);
    result.sites = std::move(*sites);
    result.siteKinds = QcBiomeMap::siteKinds(*terrain);
    result.milliseconds = timer.restart();
    deliver(std::move(result));
}
