// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/generators/QcWorldGenerators.h"

#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/content/worldgen/QcWorldTypeParser.h"
#include "worldgen/generators/QcFlatGenerator.h"
#include "worldgen/generators/QcVoidGenerator.h"
#include "worldgen/nether/QcNetherGenerator.h"
#include "worldgen/showcase/QcShowcaseGenerator.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Where the portal of the showcase leads in the nether (decision E-1034): the arrival station's
// middle, its floor one below the portal's; none without the station or the portal.
std::optional<QcBlockPos> stationOf(const std::shared_ptr<const QcContentRegistry> &content)
{
    const QString portal = content->worldgen().nether().station.portal;
    const QcDimensionDefinition *dimension = content->worldgen().dimension(u"qtcraft:nether"_s);
    if (portal.isEmpty() || dimension == nullptr) {
        return std::nullopt;
    }
    const QcShowcaseGenerator showcase(content);
    for (const QcStructurePlacement &placement : showcase.plan().placements) {
        if (placement.structure().name() == portal) {
            const QcBlockPos at = placement.origin();
            const double scale = dimension->coordinateScale;
            return QcBlockPos{static_cast<int>(std::floor(at.x / scale)), at.y, static_cast<int>(std::floor(at.z / scale))};
        }
    }
    return std::nullopt;
}

// A generator of `type`; nullptr for unknown types.
std::unique_ptr<QcWorldGenerator> makeGenerator(const QString &type, std::shared_ptr<const QcContentRegistry> content, qint64 seed,
                                                const QString &options)
{
    using namespace QcWorldGenerators;
    if (type == QLatin1StringView(terrain)) {
        return std::make_unique<QcTerrainGenerator>(std::move(content), seed);
    }
    if (type == QLatin1StringView(flat)) {
        return std::make_unique<QcFlatGenerator>(std::move(content), options);
    }
    if (type == QLatin1StringView(empty)) {
        return std::make_unique<QcVoidGenerator>(std::move(content), options);
    }
    if (type == QLatin1StringView(showcase)) {
        return std::make_unique<QcShowcaseGenerator>(std::move(content));
    }
    if (type == QLatin1StringView(nether)) {
        const std::optional<QcBlockPos> station = options == QLatin1StringView(netherStation) ? stationOf(content) : std::nullopt;
        return std::make_unique<QcNetherGenerator>(std::move(content), seed, station);
    }
    return nullptr;
}

} // namespace

namespace QcWorldGenerators {

QStringList types()
{
    return {QString::fromLatin1(terrain), QString::fromLatin1(flat), QString::fromLatin1(empty), QString::fromLatin1(showcase)};
}

bool isKnown(const QString &type)
{
    return types().contains(type);
}

[[nodiscard]] QcStatus checkOptions(const QString &type, const QString &options, const QcContentRegistry &content)
{
    if (!isKnown(type) && type != QLatin1StringView(nether)) {
        return qcError(u"Unknown world type '%1' (%2)"_s.arg(type, types().join(u", "_s)));
    }
    if (options.size() > maxOptionsLength) {
        return qcError(u"The options are longer than %1 characters"_s.arg(maxOptionsLength));
    }
    if (options.isEmpty()) {
        return {};
    }

    if (type == QLatin1StringView(flat)) {
        if (const QcResult<QList<QcBlockId>> layers = QcFlatGenerator::parseLayers(options, content); !layers) {
            return qcError(layers.error());
        }
        return {};
    }
    if (type == QLatin1StringView(empty)) {
        if (const QcResult<QcVoidDefinition> platform = QcVoidGenerator::parseOptions(options, content); !platform) {
            return qcError(platform.error());
        }
        return {};
    }
    if (type == QLatin1StringView(nether) && options == QLatin1StringView(netherStation)) {
        return {};
    }
    return qcError(u"The world type '%1' takes no options"_s.arg(type));
}

[[nodiscard]] QcResult<QString> optionsToStore(const QString &type, const QString &options, const QcContentRegistry &content)
{
    if (const QcStatus checked = checkOptions(type, options, content); !checked) {
        return qcError(checked.error());
    }

    if (type == QLatin1StringView(flat)) {
        return QcWorldTypeParser::shortPreset(options.isEmpty() ? content.worldgen().flatWorld().preset : options);
    }
    if (type == QLatin1StringView(empty)) {
        return QcVoidGenerator::optionsOf(*QcVoidGenerator::parseOptions(options, content));
    }
    return options;
}

QcGeneratorVersion::Series versionSeries(const QString &type)
{
    // Terrain, flat, void and showcase share the overworld's numbers.
    return type == QLatin1StringView(nether) ? QcGeneratorVersion::nether : QcGeneratorVersion::overworld;
}

std::unique_ptr<QcWorldGenerator> create(const QString &type, std::shared_ptr<const QcContentRegistry> content, qint64 seed,
                                         const QString &options, int version)
{
    if (!versionSeries(type).isKnown(version)) {
        return nullptr;
    }
    std::unique_ptr<QcWorldGenerator> generator = makeGenerator(type, std::move(content), seed, options);
    // Every version so far generates with the same code (decision E-922): a version that changes
    // the output branches on version() where it does. The showcase is always the current one.
    if (generator) {
        generator->setVersion(type == QLatin1StringView(showcase) ? QcGeneratorVersion::current : version);
    }
    return generator;
}

int versionForNewWorld(const QcDimensionDefinition &dimension)
{
    return versionSeries(dimension.generatorType).current;
}

std::unique_ptr<QcWorldGenerator> createDimension(const QcDimensionDefinition &dimension, std::shared_ptr<const QcContentRegistry> content, qint64 seed,
                                                  int version, bool demonstration)
{
    const bool station = demonstration && dimension.generatorType == QLatin1StringView(nether);
    const QString options = station ? QString::fromLatin1(netherStation) : dimension.generatorOptions;
    return create(dimension.generatorType, std::move(content), seed, options, version);
}

} // namespace QcWorldGenerators
