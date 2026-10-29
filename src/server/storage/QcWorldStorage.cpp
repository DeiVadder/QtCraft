// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/storage/QcWorldStorage.h"

#include "common/protocol/QcStreamLimits.h"
#include "common/world/QcGeneratorVersion.h"
#include "server/core/QcServerLogging.h"
#include "server/storage/QcSaveFile.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/generators/QcWorldOptions.h"

#include <QDir>
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
    QFile file(QDir(m_directory).filePath(u"world.json"_s));
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
    return generator;
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

} // namespace

QcResult<QcWorldInfo> QcWorldStorage::readInfo() const
{
    const QString path = QDir(m_directory).filePath(u"world.json"_s);
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

    const QJsonArray packs = json.value(u"packs"_s).toArray();
    for (const auto &entry : packs) {
        const QJsonObject pack = entry.toObject();
        if (info.packs.size() < 1024 && !pack.value(u"id"_s).toString().isEmpty()) {
            info.packs.append({pack.value(u"id"_s).toString(), pack.value(u"name"_s).toString(), pack.value(u"version"_s).toString()});
        }
    }

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
        QJsonArray loaders;
        for (const QcChunkLoaders::Loader &loader : info.chunkLoaders) {
            loaders.append(QJsonObject{{u"x"_s, loader.pos.x}, {u"y"_s, loader.pos.y}, {u"z"_s, loader.pos.z}, {u"owner"_s, loader.owner}});
        }
        json.insert(u"chunkLoaders"_s, loaders);
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
    if (!QDir().mkpath(m_directory)) {
        return qcError(u"Cannot create %1"_s.arg(m_directory));
    }

    QSaveFile file(QDir(m_directory).filePath(QString::fromLatin1(infoFileName)));
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
    const QString path = QDir(m_directory).filePath(u"blockupdates.qce"_s);
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
    const QString path = QDir(m_directory).filePath(QString::fromLatin1(villagesFileName));
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
