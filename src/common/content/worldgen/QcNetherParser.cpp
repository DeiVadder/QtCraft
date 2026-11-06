// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcNetherParser.h"

#include "common/content/registry/QcContentFields.h"
#include "common/content/registry/QcJsonFieldReader.h"
#include "common/core/QcWorldConstants.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace {

// The most entries of one list of worldgen/nether.json.
constexpr qsizetype maxEntries = 32;
constexpr int top = QcWorld::worldHeight - 1;

// The entries of the list `key`, each read by `read` with its own reader; a failure ends the list.
template <typename Entry, typename Read>
[[nodiscard]] QList<Entry> listOf(const QJsonObject &json, const QString &key, QcJsonFieldReader &reader, Read read)
{
    QList<Entry> entries;
    const QJsonValue value = json.value(key);
    if (!value.isUndefined() && (!value.isArray() || value.toArray().size() > maxEntries)) {
        reader.fail(u"'%1' must be an array of at most %2 entries"_s.arg(key).arg(maxEntries));
        return entries;
    }
    const QJsonArray array = value.toArray();
    for (const auto &item : array) {
        const QJsonObject object = item.toObject();
        QcJsonFieldReader entryReader{object, reader.context, {}};
        Entry entry = read(entryReader);
        if (!item.isObject() || !entryReader.ok()) {
            reader.adopt(entryReader);
            reader.fail(u"invalid '%1' entry"_s.arg(key));
            break;
        }
        entries.append(std::move(entry));
    }
    return entries;
}

// The block `key` of an entry, which is required.
[[nodiscard]] QString requiredBlock(QcJsonFieldReader &reader, const QString &key, const QString &packNamespace)
{
    QString block = reader.name(key, packNamespace);
    if (block.isEmpty()) {
        reader.fail(u"'%1' is required"_s.arg(key));
    }
    return block;
}

[[nodiscard]] QcNetherSettings::Shape shapeOf(QcJsonFieldReader &reader)
{
    QcNetherSettings::Shape shape;
    shape.scale = reader.number(u"scale"_s, shape.scale, 4.0, 1024.0);
    shape.verticalScale = reader.number(u"verticalScale"_s, shape.verticalScale, 4.0, 1024.0);
    shape.octaves = reader.integer(u"octaves"_s, shape.octaves, 1, 6);
    shape.floorHeight = reader.integer(u"floorHeight"_s, shape.floorHeight, 1, top);
    shape.floorFade = reader.integer(u"floorFade"_s, shape.floorFade, 1, top);
    shape.roofHeight = reader.integer(u"roofHeight"_s, shape.roofHeight, 1, top);
    shape.roofFade = reader.integer(u"roofFade"_s, shape.roofFade, 1, top);
    shape.solidity = reader.number(u"solidity"_s, shape.solidity, -1.0, 1.0);
    return shape;
}

[[nodiscard]] QcNetherSettings::Shore shoreOf(QcJsonFieldReader &reader, const QString &pack)
{
    QcNetherSettings::Shore shore;
    shore.block = requiredBlock(reader, u"block"_s, pack);
    shore.scale = reader.number(u"scale"_s, shore.scale, 1.0, 512.0);
    shore.threshold = reader.number(u"threshold"_s, shore.threshold, -1.0, 1.0);
    shore.height = reader.integer(u"height"_s, shore.height, 0, 32);
    shore.depth = reader.integer(u"depth"_s, shore.depth, 1, 8);
    return shore;
}

[[nodiscard]] QcNetherSettings::Pillar pillarOf(QcJsonFieldReader &reader, const QString &pack)
{
    QcNetherSettings::Pillar pillar;
    pillar.block = requiredBlock(reader, u"block"_s, pack);
    pillar.biomes = QcContentFields::parseNames(u"biomes"_s, pack, reader);
    pillar.scale = reader.number(u"scale"_s, pillar.scale, 1.0, 512.0);
    pillar.threshold = reader.number(u"threshold"_s, pillar.threshold, -1.0, 1.0);
    pillar.height = reader.integer(u"height"_s, pillar.height, 1, 64);
    return pillar;
}

[[nodiscard]] QcNetherSettings::Pool poolOf(QcJsonFieldReader &reader, const QString &pack)
{
    QcNetherSettings::Pool pool;
    pool.block = requiredBlock(reader, u"block"_s, pack);
    pool.rim = requiredBlock(reader, u"rim"_s, pack);
    pool.biomes = QcContentFields::parseNames(u"biomes"_s, pack, reader);
    pool.scale = reader.number(u"scale"_s, pool.scale, 1.0, 512.0);
    pool.threshold = reader.number(u"threshold"_s, pool.threshold, -1.0, 1.0);
    pool.rimBand = reader.number(u"rimBand"_s, pool.rimBand, 0.0, 1.0);
    return pool;
}

[[nodiscard]] QcNetherSettings::Cluster clusterOf(QcJsonFieldReader &reader, const QString &pack)
{
    QcNetherSettings::Cluster cluster;
    cluster.biomes = QcContentFields::parseNames(u"biomes"_s, pack, reader);
    cluster.block = requiredBlock(reader, u"block"_s, pack);
    cluster.on = requiredBlock(reader, u"on"_s, pack);
    cluster.tries = reader.integer(u"tries"_s, cluster.tries, 0, 64);
    cluster.minY = reader.integer(u"minY"_s, cluster.minY, 1, top);
    cluster.maxY = reader.integer(u"maxY"_s, cluster.maxY, cluster.minY, top);
    cluster.size = reader.integer(u"size"_s, cluster.size, 1, 256);
    cluster.radius = reader.integer(u"radius"_s, cluster.radius, 1, 7);
    cluster.depth = reader.integer(u"depth"_s, cluster.depth, 1, 16);
    return cluster;
}

[[nodiscard]] QcNetherSettings::Spring springOf(QcJsonFieldReader &reader, const QString &pack)
{
    QcNetherSettings::Spring spring;
    spring.block = requiredBlock(reader, u"block"_s, pack);
    spring.in = requiredBlock(reader, u"in"_s, pack);
    spring.tries = reader.integer(u"tries"_s, spring.tries, 0, 256);
    spring.minY = reader.integer(u"minY"_s, spring.minY, 1, top);
    spring.maxY = reader.integer(u"maxY"_s, spring.maxY, spring.minY, top);
    return spring;
}

[[nodiscard]] QcNetherSettings::Fire fireOf(const QJsonObject &json, QcJsonFieldReader &reader, const QString &pack)
{
    QcNetherSettings::Fire fire;
    fire.block = requiredBlock(reader, u"block"_s, pack);
    fire.on = QcContentFields::parseNames(u"on"_s, pack, reader);
    fire.tries = reader.integer(u"tries"_s, fire.tries, 0, 64);
    if (json.value(u"on"_s).toArray().isEmpty()) {
        reader.fail(u"'on' is required"_s);
    }
    return fire;
}

[[nodiscard]] QcNetherSettings::Structure structureOf(QcJsonFieldReader &reader, const QString &pack)
{
    QcNetherSettings::Structure structure;
    structure.structure = requiredBlock(reader, u"structure"_s, pack);
    structure.spacing = reader.integer(u"spacing"_s, structure.spacing, 32, 4096);
    structure.chance = reader.number(u"chance"_s, structure.chance, 0.0, 1.0);
    return structure;
}

} // namespace

namespace QcNetherParser {

QcResult<QcNetherSettings> parse(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"nether"_s, {}};
    QcNetherSettings nether;
    nether.filler = reader.name(u"filler"_s, packNamespace, nether.filler);
    nether.bedrock = reader.name(u"bedrock"_s, packNamespace, nether.bedrock);
    nether.liquid = reader.name(u"liquid"_s, packNamespace, nether.liquid);
    nether.liquidLevel = reader.integer(u"liquidLevel"_s, nether.liquidLevel, 1, top - 8);
    nether.roof = reader.integer(u"roof"_s, nether.roof, nether.liquidLevel + 8, top);
    nether.bedrockLayers = reader.integer(u"bedrockLayers"_s, nether.bedrockLayers, 1, 8);
    nether.biomeScale = reader.number(u"biomeScale"_s, nether.biomeScale, 8.0, 4096.0);
    reader.nestedObject(u"shape"_s, [&nether](const QJsonObject &, QcJsonFieldReader &nested) { nether.shape = shapeOf(nested); });
    nether.shores = listOf<QcNetherSettings::Shore>(json, u"shores"_s, reader, [&](QcJsonFieldReader &entry) { return shoreOf(entry, packNamespace); });
    nether.clusters = listOf<QcNetherSettings::Cluster>(json, u"clusters"_s, reader, [&](QcJsonFieldReader &entry) { return clusterOf(entry, packNamespace); });
    nether.springs = listOf<QcNetherSettings::Spring>(json, u"springs"_s, reader, [&](QcJsonFieldReader &entry) { return springOf(entry, packNamespace); });
    nether.pillars = listOf<QcNetherSettings::Pillar>(json, u"pillars"_s, reader, [&](QcJsonFieldReader &entry) { return pillarOf(entry, packNamespace); });
    nether.pools = listOf<QcNetherSettings::Pool>(json, u"pools"_s, reader, [&](QcJsonFieldReader &entry) { return poolOf(entry, packNamespace); });
    nether.fires = listOf<QcNetherSettings::Fire>(json, u"fires"_s, reader, [&](QcJsonFieldReader &entry) { return fireOf(entry.json, entry, packNamespace); });
    nether.structures = listOf<QcNetherSettings::Structure>(json, u"structures"_s, reader, [&](QcJsonFieldReader &entry) { return structureOf(entry, packNamespace); });
    reader.nestedObject(u"station"_s, [&nether, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        nether.station.structure = requiredBlock(nested, u"structure"_s, packNamespace);
        nether.station.portal = requiredBlock(nested, u"portal"_s, packNamespace);
    });
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return nether;
}

} // namespace QcNetherParser
