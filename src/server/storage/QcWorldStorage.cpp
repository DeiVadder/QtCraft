// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/storage/QcWorldStorage.h"

#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/protocol/QcStreamLimits.h"
#include "common/world/QcGeneratorVersion.h"
#include "server/core/QcServerLogging.h"
#include "server/storage/QcSaveFile.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/generators/QcWorldOptions.h"

#include <QDir>
#include <QRegularExpression>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr QByteArrayView blockUpdatesMagic("QCBU");
// More forced chunks than any limit allows are damaged data.
constexpr qsizetype maxForcedChunks = 65536;


// The chunk loader blocks of world.json; entries out of range are left out.
[[nodiscard]] QList<QcChunkLoaders::Loader> chunkLoadersOf(const QJsonObject &json)
{
    QList<QcChunkLoaders::Loader> result;
    const QJsonArray loaders = json.value(u"chunkLoaders"_s).toArray();
    for (const auto &entry : loaders) {
        const QJsonObject loader = entry.toObject();
        const QcBlockPos pos{loader.value(u"x"_s).toInt(), loader.value(u"y"_s).toInt(), loader.value(u"z"_s).toInt()};
        if (result.size() < maxForcedChunks && pos.isInsideWorldHeight() && std::abs(static_cast<qint64>(pos.x)) <= QcWorld::maxChunkBlock
            && std::abs(static_cast<qint64>(pos.z)) <= QcWorld::maxChunkBlock) {
            result.append({pos, loader.value(u"owner"_s).toString().left(64)});
        }
    }
    return result;
}

// The forced chunks of world.json; entries out of range and repeated ones are left out.
[[nodiscard]] QList<QcChunkPos> forcedChunksOf(const QJsonObject &json)
{
    QList<QcChunkPos> result;
    const QJsonArray forced = json.value(u"forcedChunks"_s).toArray();
    for (const auto &entry : forced) {
        const QJsonArray pair = entry.toArray();
        const QcChunkPos chunk{pair.at(0).toInt(), pair.at(1).toInt()};
        if (result.size() < maxForcedChunks && pair.size() == 2 && std::abs(static_cast<qint64>(chunk.x)) <= QcWorld::maxHorizontalChunk
            && std::abs(static_cast<qint64>(chunk.z)) <= QcWorld::maxHorizontalChunk && !result.contains(chunk)) {
            result.append(chunk);
        }
    }
    return result;
}

} // namespace

QcWorldStorage::QcWorldStorage(QString directory, std::shared_ptr<const QcContentRegistry> content)
    : m_directory(std::move(directory))
    , m_content(std::move(content))
    , m_players(m_directory, m_content)
{
}

int QcWorldStorage::storedFormatVersion() const
{
    QFile file(QDir(directory()).filePath(u"world.json"_s));
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.read(qint64{1024} * 1024));
    return document.isObject() ? document.object().value(u"formatVersion"_s).toInt() : 0;
}

namespace {

// The adventure allow-lists of world.json.
QcAdventureRules adventureOf(const QJsonValue &value, const QString &path)
{
    // A damaged entry is left out with a warning; the rest of the world still opens (decision E-735).
    if (!value.isUndefined() && !value.isObject()) {
        qCWarning(lcServer, "%s: \"adventure\" is no object; the world allows nothing in adventure mode", qPrintable(path));
        return {};
    }

    QcAdventureRules::Read read = QcAdventureRules::fromJson(value.toObject());
    for (const QString &problem : std::as_const(read.problems)) {
        qCWarning(lcServer, "%s: adventure allow-list %s", qPrintable(path), qPrintable(problem));
    }
    return std::move(read.rules);
}

bool isShowcase(const QString &type)
{
    return type == QLatin1StringView(QcWorldGenerators::showcase);
}

// "generator" of world.json: the type, the generator version (decision E-922; not for the
// showcase, which always generates with the current code), its options, the world generation options
// (only when the world has them, decision E-917) and whether the setup has run.
QJsonObject generatorJson(const QcWorldInfo &info)
{
    QJsonObject generator{{u"type"_s, info.generator}, {u"options"_s, info.generatorOptions}, {u"setupDone"_s, info.setupDone}};
    if (!isShowcase(info.generator)) {
        generator.insert(u"version"_s, info.generatorVersion);
    }
    if (!info.worldOptions.isEmpty()) {
        generator.insert(u"worldOptions"_s, info.worldOptions);
    }
    if (!info.dimensionGeneratorVersions.isEmpty()) {
        QJsonObject dimensions;
        for (auto it = info.dimensionGeneratorVersions.begin(); it != info.dimensionGeneratorVersions.end(); ++it) {
            dimensions.insert(it.key(), QJsonObject{{u"version"_s, it.value()}});
        }
        generator.insert(u"dimensions"_s, dimensions);
    }
    return generator;
}

// The content packs world.json names ("packs"), at most 1024, those without id left out.
QList<QcPackReference> packsOf(const QJsonObject &json)
{
    QList<QcPackReference> references;
    const QJsonArray packs = json.value(u"packs"_s).toArray();
    for (const auto &entry : packs) {
        const QJsonObject pack = entry.toObject();
        if (references.size() < 1024 && !pack.value(u"id"_s).toString().isEmpty()) {
            references.append({pack.value(u"id"_s).toString(), pack.value(u"name"_s).toString(), pack.value(u"version"_s).toString()});
        }
    }
    return references;
}

// The generator versions of the other dimensions (decision E-979, "generator"/"dimensions":
// {"<name>": {"version": n}}). Whether this game generates a version is checked when the dimension
// opens (its series depends on its generator); here only damage is refused.
QcResult<QMap<QString, int>> dimensionVersionsOf(const QJsonObject &generator, const QString &path)
{
    const QJsonValue value = generator.value(u"dimensions"_s);
    if (value.isUndefined()) {
        return QMap<QString, int>{};
    }
    const QString damaged = u"%1 is damaged: the generator versions of the dimensions"_s.arg(path);
    if (!value.isObject() || value.toObject().size() > QcWorldInfo::maxDimensionVersions) {
        return qcError(damaged);
    }
    QMap<QString, int> versions;
    const QJsonObject dimensions = value.toObject();
    for (auto it = dimensions.begin(); it != dimensions.end(); ++it) {
        const QJsonValue version = it.value().toObject().value(u"version"_s);
        const double number = version.toDouble(-1.0);
        if (it.key().isEmpty() || it.key().size() > QcDimensionDefinition::maxNameLength || !version.isDouble() || number != std::floor(number) || number < 1.0
            || number > QcGeneratorVersion::maxStored) {
            return qcError(damaged);
        }
        versions.insert(it.key(), static_cast<int>(number));
    }
    return versions;
}

// The generator version of world.json's "generator" (decision E-922): worlds without one are of
// QcGeneratorVersion::unversioned, a showcase world is always of the current one; a version this game does not generate (a newer game's, or one
// it no longer has) is refused with the reason, a number that is none is damage.
QcResult<int> generatorVersionOf(const QJsonObject &generator, const QString &path)
{
    if (isShowcase(generator.value(u"type"_s).toString())) {
        return QcGeneratorVersion::current;
    }
    const QJsonValue value = generator.value(u"version"_s);
    if (value.isUndefined()) {
        return QcGeneratorVersion::unversioned;
    }
    const double number = value.toDouble(-1.0);
    if (!value.isDouble() || number != std::floor(number) || number < 1.0 || number > QcGeneratorVersion::maxStored) {
        return qcError(u"%1 is damaged: the generator version is no whole number from 1 to %2"_s.arg(path).arg(QcGeneratorVersion::maxStored));
    }
    const int version = static_cast<int>(number);
    if (version > QcGeneratorVersion::current) {
        return qcError(u"%1 was made by a newer game with world generator version %2; this game generates versions %3 to %4"_s.arg(path)
                           .arg(version)
                           .arg(QcGeneratorVersion::oldest)
                           .arg(QcGeneratorVersion::current));
    }
    if (version < QcGeneratorVersion::oldest) {
        return qcError(u"%1 was made with world generator version %2, which this game no longer has (oldest: %3)"_s.arg(path)
                           .arg(version)
                           .arg(QcGeneratorVersion::oldest));
    }
    return version;
}

// The chunk loader blocks as world.json and dimension.json keep them.
QJsonArray chunkLoadersJson(const QList<QcPlacedChunkLoader> &chunkLoaders)
{
    QJsonArray loaders;
    for (const QcPlacedChunkLoader &loader : chunkLoaders) {
        loaders.append(QJsonObject{{u"x"_s, loader.pos.x}, {u"y"_s, loader.pos.y}, {u"z"_s, loader.pos.z}, {u"owner"_s, loader.owner}});
    }
    return loaders;
}

// A part of a dimension's name as a folder on every system: Windows opens the device instead of a
// file or folder named like one, in any case (decision E-988).
QString folderName(const QString &part)
{
    static const QRegularExpression reserved(u"^(con|prn|aux|nul|conin\\$|conout\\$|com[0-9]|lpt[0-9])$"_s, QRegularExpression::CaseInsensitiveOption);
    return reserved.match(part).hasMatch() ? part + u'~' : part;
}

} // namespace

QString QcWorldStorage::dimensionDirectory(const QString &worldDirectory, const QcDimensionDefinition &dimension)
{
    if (dimension.isOverworld()) {
        return worldDirectory;
    }
    // The namespace becomes a folder of its own (QcDimensionParser refuses '/' and '.' in a name).
    QStringList parts = dimension.name.split(u':');
    for (QString &part : parts) {
        part = folderName(part);
    }
    return QDir(worldDirectory).filePath(QString::fromLatin1(dimensionsFolder) + u'/' + parts.join(u'/'));
}

QcResult<QcWorldStorage::DimensionFile> QcWorldStorage::readDimensionFile() const
{
    QFile file(QDir(directory()).filePath(QString::fromLatin1(dimensionFileName)));
    if (!file.exists()) {
        return DimensionFile{};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"Cannot open %1: %2"_s.arg(file.fileName(), file.errorString()));
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.read(qint64{1024} * 1024), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return qcError(u"%1 is damaged"_s.arg(file.fileName()));
    }
    const QJsonObject json = document.object();
    return DimensionFile{json.value(u"setupDone"_s).toBool(true), chunkLoadersOf(json)};
}

QByteArray QcWorldStorage::dimensionFileBytes(const DimensionFile &file)
{
    QJsonObject json{{u"setupDone"_s, file.setupDone}};
    if (!file.chunkLoaders.isEmpty()) {
        json.insert(u"chunkLoaders"_s, chunkLoadersJson(file.chunkLoaders));
    }
    return QJsonDocument(json).toJson();
}

QcResult<QcWorldInfo> QcWorldStorage::readInfo() const
{
    const QString path = QDir(directory()).filePath(u"world.json"_s);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"Cannot open %1: %2"_s.arg(path, file.errorString()));
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.read(qint64{1024} * 1024), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        const QString reason = error.error != QJsonParseError::NoError ? error.errorString() : u"not a JSON object"_s;
        return qcError(u"%1 is damaged: %2"_s.arg(path, reason));
    }

    const QJsonObject json = document.object();
    const int version = json.value(u"formatVersion"_s).toInt();
    if (version < QcWorldInfo::oldestFormatVersion || version > QcWorldInfo::formatVersion) {
        return qcError(u"%1 has unsupported format version %2"_s.arg(path).arg(version));
    }

    bool seedOk = false;
    QcWorldInfo info;
    info.name = json.value(u"name"_s).toString();
    info.seed = json.value(u"seed"_s).toString().toLongLong(&seedOk);
    info.created = QDateTime::fromString(json.value(u"created"_s).toString(), Qt::ISODate);
    info.lastPlayed = QDateTime::fromString(json.value(u"lastPlayed"_s).toString(), Qt::ISODate);
    info.worldTime = std::max(0.0, json.value(u"worldTime"_s).toDouble());
    info.readGameRules(json.value(u"gameRules"_s).toObject());
    info.weather = QcWeatherCycle::fromJson(json.value(u"weather"_s).toObject());
    const QJsonObject generator = json.value(u"generator"_s).toObject();
    info.generator = generator.value(u"type"_s).toString();
    info.setupDone = generator.value(u"setupDone"_s).toBool(true);
    info.generatorOptions = generator.value(u"options"_s).toString();
    info.worldOptions = generator.value(u"worldOptions"_s).toObject();
    const QcResult<int> generatorVersion = generatorVersionOf(generator, path);
    if (!generatorVersion) {
        return qcError(generatorVersion.error());
    }
    info.generatorVersion = *generatorVersion;
    QcResult<QMap<QString, int>> dimensionVersions = dimensionVersionsOf(generator, path);
    if (!dimensionVersions) {
        return qcError(dimensionVersions.error());
    }
    info.dimensionGeneratorVersions = std::move(*dimensionVersions);
    // A mode this version does not know (a newer one) plays as survival and keeps its name.
    const QString gameMode = json.value(u"defaultGameMode"_s).toString();
    const std::optional<QcGameMode> knownMode = QcGameModes::parse(gameMode);
    info.defaultGameMode = knownMode.value_or(QcGameMode::Survival);
    info.unknownGameMode = knownMode || gameMode.isEmpty() ? QString() : gameMode.left(64);
    info.adventure = adventureOf(json.value(u"adventure"_s), path);
    info.limits = json.value(u"limits"_s).toObject();
    bool idOk = false;
    info.nextCreatureId = json.value(u"nextCreatureId"_s).toString().toULongLong(&idOk);
    // Worlds without the field start at 1 (the map files raise it, QcServerPersistence::openMaps).
    info.nextMapId = static_cast<quint32>(std::clamp<qint64>(json.value(u"nextMapId"_s).toInteger(1), 1, qint64{1} << 31));
    info.chunkLoaders = chunkLoadersOf(json);
    info.forcedChunks = forcedChunksOf(json);

    info.packs = packsOf(json);

    if (!seedOk || !idOk || info.nextCreatureId == 0 || info.name.isEmpty() || info.generator.isEmpty() || info.generator.size() > 64 || info.generatorOptions.size() > QcWorldGenerators::maxOptionsLength
        || info.worldOptions.size() > QcWorldOptions::maxValues) {
        return qcError(u"%1 is damaged: missing name, seed, next creature id or generator"_s.arg(path));
    }
    return info;
}

QByteArray QcWorldStorage::infoBytes(const QcWorldInfo &info)
{
    QJsonArray packs;
    for (const QcPackReference &pack : info.packs) {
        packs.append(QJsonObject{{u"id"_s, pack.id}, {u"name"_s, pack.name}, {u"version"_s, pack.version}});
    }

    QJsonObject json{{u"formatVersion"_s, QcWorldInfo::formatVersion},
                           {u"packs"_s, packs},
                           {u"name"_s, info.name},
                           // As a string: JSON numbers cannot hold every 64-bit seed exactly.
                           {u"seed"_s, QString::number(info.seed)},
                           {u"created"_s, info.created.toString(Qt::ISODate)},
                           {u"lastPlayed"_s, info.lastPlayed.toString(Qt::ISODate)},
                           {u"worldTime"_s, info.worldTime},
                           // As a string like the seed.
                           {u"nextCreatureId"_s, QString::number(info.nextCreatureId)},
                           {u"nextMapId"_s, static_cast<qint64>(info.nextMapId)},
                           {u"defaultGameMode"_s, info.unknownGameMode.isEmpty() ? QString(QcGameModes::name(info.defaultGameMode)) : info.unknownGameMode},
                           {u"generator"_s, generatorJson(info)},
                           {u"gameRules"_s, QJsonObject{{u"keepInventory"_s, info.keepInventory},
                                                        {u"daylightCycle"_s, info.daylightCycle},
                                                        {u"mobSpawning"_s, info.mobSpawning},
                                                        {u"randomTicks"_s, info.randomTicks},
                                                        {u"fireSpread"_s, info.fireSpread},
                                                        {u"quasiConnectivity"_s, info.quasiConnectivity},
                                                        {u"weatherCycle"_s, info.weatherCycle},
                                                        {u"mobGriefing"_s, info.mobGriefing},
                                                        {u"explosionBlockDamage"_s, info.explosionBlockDamage},
                                                        {u"drowningDamage"_s, info.drowningDamage},
                                                        {u"announceAdvancements"_s, info.announceAdvancements},
                                                        {u"spawnChunkRadius"_s, info.spawnChunkRadius},
                                                        {u"maxEntityCramming"_s, info.maxEntityCramming},
                                                        {u"playerInvulnerabilityTicks"_s, info.playerInvulnerabilityTicks},
                                                        {u"mobInvulnerabilityTicks"_s, info.mobInvulnerabilityTicks}}}};
    json.insert(u"weather"_s, QcWeatherCycle::toJson(info.weather));
    if (!info.limits.isEmpty()) {
        json.insert(u"limits"_s, info.limits);
    }
    if (!info.adventure.isEmpty()) {
        json.insert(u"adventure"_s, info.adventure.toJson());
    }
    if (!info.chunkLoaders.isEmpty()) {
        json.insert(u"chunkLoaders"_s, chunkLoadersJson(info.chunkLoaders));
    }
    if (!info.forcedChunks.isEmpty()) {
        QJsonArray forced;
        for (const QcChunkPos &chunk : info.forcedChunks) {
            forced.append(QJsonArray{chunk.x, chunk.z});
        }
        json.insert(u"forcedChunks"_s, forced);
    }

    return QJsonDocument(json).toJson();
}

QcStatus QcWorldStorage::writeInfo(const QcWorldInfo &info) const
{
    if (!QDir().mkpath(directory())) {
        return qcError(u"Cannot create %1"_s.arg(directory()));
    }

    QSaveFile file(QDir(directory()).filePath(QString::fromLatin1(infoFileName)));
    if (!file.open(QIODevice::WriteOnly) || file.write(infoBytes(info)) < 0 || !file.commit()) {
        return qcError(u"Cannot write world info: %1"_s.arg(file.errorString()));
    }
    return {};
}

QcResult<std::optional<QByteArray>> QcWorldStorage::blockUpdatesBytes(const QList<QcBlockPos> &positions)
{
    if (positions.size() > static_cast<qsizetype>(maxStoredBlockUpdates)) {
        return qcError(u"Too many pending block updates to save (%1, at most %2)"_s.arg(positions.size()).arg(maxStoredBlockUpdates));
    }
    if (positions.isEmpty()) {
        // No file means no pending updates.
        return std::optional<QByteArray>();
    }

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    stream << static_cast<quint32>(positions.size());
    for (const QcBlockPos &pos : positions) {
        stream << static_cast<qint32>(pos.x) << static_cast<qint32>(pos.y) << static_cast<qint32>(pos.z);
    }
    return std::optional<QByteArray>(QcSaveFile::bytes(blockUpdatesMagic, payload));
}

QcResult<QList<QcBlockPos>> QcWorldStorage::readBlockUpdates() const
{
    const QString path = QDir(directory()).filePath(u"blockupdates.qce"_s);
    if (!QFile::exists(path)) {
        return QList<QcBlockPos>();
    }

    const QcResult<QcSaveFile::Payload> payload = QcSaveFile::read(path, blockUpdatesMagic);
    if (!payload) {
        return qcError(payload.error());
    }

    QDataStream stream(payload->bytes);
    stream.setVersion(QcSaveFile::streamVersion);
    const std::optional<quint32> countRead = QcStreamLimits::readCount(stream, maxStoredBlockUpdates);
    if (!countRead) {
        return qcError(u"%1 is damaged (count)"_s.arg(path));
    }
    const quint32 count = *countRead;

    QList<QcBlockPos> positions;
    positions.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        qint32 x = 0;
        qint32 y = 0;
        qint32 z = 0;
        stream >> x >> y >> z;

        const QcBlockPos pos{x, y, z};
        if (stream.status() != QDataStream::Ok || !pos.isInsideWorldHeight() || std::abs(static_cast<qint64>(x)) > QcWorld::maxChunkBlock
            || std::abs(static_cast<qint64>(z)) > QcWorld::maxChunkBlock) {
            return qcError(u"%1 is damaged (position %2)"_s.arg(path).arg(i));
        }
        positions.append(pos);
    }

    if (!stream.atEnd()) {
        return qcError(u"%1 is damaged"_s.arg(path));
    }
    return positions;
}

QcResult<QList<QcVillage>> QcWorldStorage::readVillages(const QcVillage::ReadLimits &limits) const
{
    const QString path = QDir(directory()).filePath(QString::fromLatin1(villagesFileName));
    QFile file(path);
    if (!file.exists()) {
        return QList<QcVillage>();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"%1 cannot be read"_s.arg(path));
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return qcError(u"%1 is damaged: %2"_s.arg(path, error.errorString()));
    }

    QList<QcVillage> villages;
    const QJsonArray entries = document.object().value(u"villages"_s).toArray();
    for (const auto &entry : entries) {
        if (std::optional<QcVillage> village = QcVillage::fromJson(entry.toObject(), limits)) {
            villages.append(std::move(*village));
        }
    }
    return villages;
}

QByteArray QcWorldStorage::villagesBytes(const QList<QcVillage> &villages)
{
    QJsonArray array;
    for (const QcVillage &village : villages) {
        array.append(village.toJson());
    }
    return QJsonDocument(QJsonObject{{u"formatVersion"_s, 1}, {u"villages"_s, array}}).toJson(QJsonDocument::Indented);
}
