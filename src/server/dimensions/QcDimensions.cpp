// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/dimensions/QcDimensions.h"

#include "worldgen/generators/QcWorldGenerators.h"

#include <utility>

using namespace Qt::StringLiterals;

namespace {

// What is drawn at random in the dimension `name` starts from: the world's seed in the overworld, as
// before dimensions, elsewhere mixed with the name (FNV-1a), the same in every run.
quint64 seedOf(const QString &name, quint64 seed)
{
    if (name == QLatin1StringView(QcDimensionDefinition::overworldName)) {
        return seed;
    }
    quint64 hash = 0xCBF29CE484222325ull;
    for (const QChar character : name) {
        hash = (hash ^ character.unicode()) * 0x100000001B3ull;
    }
    return seed ^ hash;
}

// What is wrong with the generator of `dimension`.
QStringList generatorWarnings(const QcDimensionDefinition &dimension, const QcContentRegistry &content)
{
    const bool ownType = dimension.generatorType == QLatin1StringView(QcDimensionDefinition::worldGenerator);
    if (dimension.isOverworld()) {
        return ownType ? QStringList() : QStringList{u"The dimension '%1' names the generator '%2'; it always generates as the world was created"_s.arg(dimension.name, dimension.generatorType)};
    }
    if (ownType) {
        return {u"The dimension '%1' cannot use the generator '%2', only the overworld can; it will not open"_s.arg(dimension.name, dimension.generatorType)};
    }
    if (const QcStatus options = QcWorldGenerators::checkOptions(dimension.generatorType, dimension.generatorOptions, content); !options) {
        return {u"The dimension '%1' cannot be generated: %2; it will not open"_s.arg(dimension.name, options.error())};
    }
    return {};
}

// What `dimension` names of the blocks and is not in `content`.
QStringList blockWarnings(const QcDimensionDefinition &dimension, const QcContentRegistry &content)
{
    QStringList warnings;
    if (dimension.arrival && !dimension.arrival->platform.isEmpty() && !content.blockId(dimension.arrival->platform)) {
        warnings.append(u"The dimension '%1' names the unknown block '%2' for its arrival platform"_s.arg(dimension.name, dimension.arrival->platform));
    }
    for (const QcDimensionDefinition::LiquidFlow &flow : dimension.liquidFlows) {
        if (!content.blockId(flow.liquid)) {
            warnings.append(u"The dimension '%1' names the unknown liquid '%2'"_s.arg(dimension.name, flow.liquid));
        }
    }
    return warnings;
}

} // namespace

QcDimensions::QcDimensions(std::shared_ptr<const QcContentRegistry> content, std::unique_ptr<QcWorldGenerator> overworldGenerator, quint64 seed)
    : m_content(std::move(content))
{
    const QcDimensionDefinition &overworld = m_content->worldgen().overworld();
    addOpen(std::make_unique<QcServerDimension>(overworld, m_content, std::move(overworldGenerator), seed));
}

void QcDimensions::addOpen(std::unique_ptr<QcServerDimension> dimension)
{
    dimension->entities().shareIds(m_nextEntityId);
    dimension->entities().creatures().shareUids(m_nextCreatureUid);
    m_open.push_back(std::move(dimension));
}

QcServerDimension *QcDimensions::openNamed(const QString &name) const
{
    for (const std::unique_ptr<QcServerDimension> &dimension : all()) {
        if (dimension->name() == name) {
            return dimension.get();
        }
    }
    return nullptr;
}

QcResult<QcServerDimension *> QcDimensions::open(const QString &name, qint64 seed, const QcWorldInfo &info)
{
    const QcDimensionDefinition *definition = content()->worldgen().dimension(name);
    if (definition == nullptr) {
        return qcError(u"There is no dimension '%1'"_s.arg(name));
    }
    if (std::ssize(all()) >= maxOpen) {
        return qcError(u"Cannot open the dimension '%1': %2 dimensions are open"_s.arg(name).arg(maxOpen));
    }
    const QMap<QString, int> &versions = info.dimensionGeneratorVersions;
    if (isVersioned(info) && !versions.contains(name) && versions.size() >= QcWorldInfo::maxDimensionVersions) {
        return qcError(u"Cannot open the dimension '%1': the world keeps the versions of %2 dimensions"_s.arg(name).arg(versions.size()));
    }

    // Only the overworld generates as the world's own type, and it is open from the start.
    const QString &type = definition->generatorType;
    if (const QcStatus options = QcWorldGenerators::checkOptions(type, definition->generatorOptions, *content()); !options) {
        return qcError(u"The dimension '%1' cannot be generated: %2"_s.arg(name, options.error()));
    }
    const int generatorVersion = versionIn(info, *definition);
    const QcGeneratorVersion::Series series = QcWorldGenerators::versionSeries(type);
    if (!series.isKnown(generatorVersion)) {
        return qcError(u"The dimension '%1' was made with world generator version %2; this game generates versions %3 to %4"_s.arg(name)
                           .arg(generatorVersion)
                           .arg(series.oldest)
                           .arg(series.current));
    }

    std::unique_ptr<QcWorldGenerator> generator = QcWorldGenerators::create(type, content(), seed, definition->generatorOptions, generatorVersion);
    if (!generator) {
        return qcError(u"The dimension '%1' cannot be generated with the world type '%2'"_s.arg(name, type));
    }
    addOpen(std::make_unique<QcServerDimension>(*definition, content(), std::move(generator), seedOf(name, static_cast<quint64>(seed))));
    return all().back().get();
}

QString QcDimensions::generatorTypeIn(const QcWorldInfo &info, const QcDimensionDefinition &dimension)
{
    return dimension.generatorType == QLatin1StringView(QcDimensionDefinition::worldGenerator) ? info.generator : dimension.generatorType;
}

bool QcDimensions::isVersioned(const QcWorldInfo &info)
{
    return info.generator != QLatin1StringView(QcWorldGenerators::showcase);
}

int QcDimensions::versionIn(const QcWorldInfo &info, const QcDimensionDefinition &dimension)
{
    if (dimension.isOverworld()) {
        return info.generatorVersion;
    }
    const QcGeneratorVersion::Series series = QcWorldGenerators::versionSeries(generatorTypeIn(info, dimension));
    if (!isVersioned(info)) {
        return series.current;
    }
    return info.dimensionGeneratorVersions.value(dimension.name, series.forLateDimension());
}

QMap<QString, int> QcDimensions::versionsForNewWorld(const QcContentRegistry &content, const QcWorldInfo &info)
{
    QMap<QString, int> versions = info.dimensionGeneratorVersions;
    if (!isVersioned(info)) {
        return versions;
    }
    for (const QcDimensionDefinition &dimension : content.worldgen().dimensions()) {
        if (versions.size() >= QcWorldInfo::maxDimensionVersions) {
            break;
        }
        if (!dimension.isOverworld() && !versions.contains(dimension.name)) {
            versions.insert(dimension.name, QcWorldGenerators::versionSeries(dimension.generatorType).current);
        }
    }
    return versions;
}

QStringList QcDimensions::definitionWarnings(const QcContentRegistry &content)
{
    QStringList warnings;
    for (const QcDimensionDefinition &dimension : content.worldgen().dimensions()) {
        warnings.append(generatorWarnings(dimension, content));
        warnings.append(blockWarnings(dimension, content));
    }
    return warnings;
}

QStringList QcDimensions::versionWarnings(const QcContentRegistry &content, const QcWorldInfo &info)
{
    QStringList warnings;
    if (!isVersioned(info) || info.dimensionGeneratorVersions.size() < QcWorldInfo::maxDimensionVersions) {
        return warnings;
    }
    for (const QcDimensionDefinition &dimension : content.worldgen().dimensions()) {
        if (!dimension.isOverworld() && !info.dimensionGeneratorVersions.contains(dimension.name)) {
            warnings.append(u"The world keeps the versions of %1 dimensions: the dimension '%2' will not open"_s.arg(info.dimensionGeneratorVersions.size()).arg(dimension.name));
        }
    }
    return warnings;
}
