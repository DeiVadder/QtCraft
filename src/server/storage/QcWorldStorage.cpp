#include "QcWorldStorage.h"

#include "QcSaveFile.h"
#include "QcStreamLimits.h"
#include "QcWorldGenerators.h"

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
    const QJsonDocument document = QJsonDocument::fromJson(file.read(qint64(1024) * 1024));
    return document.isObject() ? document.object().value(u"formatVersion"_s).toInt() : 0;
}

QcResult<QcWorldInfo> QcWorldStorage::readInfo() const
{
    const QString path = QDir(m_directory).filePath(u"world.json"_s);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"Cannot open %1: %2"_s.arg(path, file.errorString()));
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.read(qint64(1024) * 1024), &error);
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
    const QJsonObject rules = json.value(u"gameRules"_s).toObject();
    info.keepInventory = rules.value(u"keepInventory"_s).toBool(true);
    info.daylightCycle = rules.value(u"daylightCycle"_s).toBool(true);
    info.mobSpawning = rules.value(u"mobSpawning"_s).toBool(true);
    info.randomTicks = rules.value(u"randomTicks"_s).toBool(true);
    info.fireSpread = rules.value(u"fireSpread"_s).toBool(true);
    info.quasiConnectivity = rules.value(u"quasiConnectivity"_s).toBool(true);
    const QJsonObject generator = json.value(u"generator"_s).toObject();
    info.generator = generator.value(u"type"_s).toString();
    info.setupDone = generator.value(u"setupDone"_s).toBool(true);
    info.generatorOptions = generator.value(u"options"_s).toString();
    // A mode this version does not know (a newer one) plays as survival and keeps its name.
    const QString gameMode = json.value(u"defaultGameMode"_s).toString();
    const std::optional<QcGameMode> knownMode = QcGameModes::parse(gameMode);
    info.defaultGameMode = knownMode.value_or(QcGameMode::Survival);
    info.unknownGameMode = knownMode || gameMode.isEmpty() ? QString() : gameMode.left(64);
    info.limits = json.value(u"limits"_s).toObject();
    info.spawnChunkRadius = std::clamp(rules.value(u"spawnChunkRadius"_s).toInt(0), 0, QcWorldInfo::maxSpawnChunkRadius);
    const QJsonArray loaders = json.value(u"chunkLoaders"_s).toArray();
    for (const auto &entry : loaders) {
        const QJsonObject loader = entry.toObject();
        const QcBlockPos pos{loader.value(u"x"_s).toInt(), loader.value(u"y"_s).toInt(), loader.value(u"z"_s).toInt()};
        if (info.chunkLoaders.size() < maxForcedChunks && pos.isInsideWorldHeight() && std::abs(qint64(pos.x)) <= QcWorld::maxHorizontalBlock
            && std::abs(qint64(pos.z)) <= QcWorld::maxHorizontalBlock) {
            info.chunkLoaders.append({pos, loader.value(u"owner"_s).toString().left(64)});
        }
    }
    const QJsonArray forced = json.value(u"forcedChunks"_s).toArray();
    for (const auto &entry : forced) {
        const QJsonArray pair = entry.toArray();
        const QcChunkPos chunk{pair.at(0).toInt(), pair.at(1).toInt()};
        if (info.forcedChunks.size() < maxForcedChunks && pair.size() == 2 && std::abs(qint64(chunk.x)) <= QcWorld::maxHorizontalChunk
            && std::abs(qint64(chunk.z)) <= QcWorld::maxHorizontalChunk && !info.forcedChunks.contains(chunk)) {
            info.forcedChunks.append(chunk);
        }
    }

    const QJsonArray packs = json.value(u"packs"_s).toArray();
    for (const auto &entry : packs) {
        const QJsonObject pack = entry.toObject();
        if (info.packs.size() < 1024 && !pack.value(u"id"_s).toString().isEmpty()) {
            info.packs.append({pack.value(u"id"_s).toString(), pack.value(u"name"_s).toString(), pack.value(u"version"_s).toString()});
        }
    }

    if (!seedOk || info.name.isEmpty() || info.generator.isEmpty() || info.generator.size() > 64 || info.generatorOptions.size() > QcWorldGenerators::maxOptionsLength) {
        return qcError(u"%1 is damaged: missing name, seed or generator"_s.arg(path));
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
                           {u"defaultGameMode"_s, info.unknownGameMode.isEmpty() ? QString(QcGameModes::name(info.defaultGameMode)) : info.unknownGameMode},
                           {u"generator"_s, QJsonObject{{u"type"_s, info.generator}, {u"options"_s, info.generatorOptions}, {u"setupDone"_s, info.setupDone}}},
                           {u"gameRules"_s, QJsonObject{{u"keepInventory"_s, info.keepInventory},
                                                        {u"daylightCycle"_s, info.daylightCycle},
                                                        {u"mobSpawning"_s, info.mobSpawning},
                                                        {u"randomTicks"_s, info.randomTicks},
                                                        {u"fireSpread"_s, info.fireSpread},
                                                        {u"quasiConnectivity"_s, info.quasiConnectivity},
                                                        {u"spawnChunkRadius"_s, info.spawnChunkRadius}}}};
    if (!info.limits.isEmpty()) {
        json.insert(u"limits"_s, info.limits);
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
    if (positions.size() > qsizetype(maxStoredBlockUpdates)) {
        return qcError(u"Too many pending block updates to save (%1, at most %2)"_s.arg(positions.size()).arg(maxStoredBlockUpdates));
    }
    if (positions.isEmpty()) {
        // No file means no pending updates.
        return std::optional<QByteArray>();
    }

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    stream << quint32(positions.size());
    for (const QcBlockPos &pos : positions) {
        stream << qint32(pos.x) << qint32(pos.y) << qint32(pos.z);
    }
    return std::optional<QByteArray>(QcSaveFile::bytes(blockUpdatesMagic, payload));
}

QcResult<QList<QcBlockPos>> QcWorldStorage::readBlockUpdates() const
{
    const QString path = QDir(m_directory).filePath(u"blockupdates.qce"_s);
    if (!QFile::exists(path)) {
        return QList<QcBlockPos>();
    }

    const QcResult<QByteArray> payload = QcSaveFile::read(path, blockUpdatesMagic);
    if (!payload) {
        return qcError(payload.error());
    }

    QDataStream stream(*payload);
    stream.setVersion(QcSaveFile::streamVersion);
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, maxStoredBlockUpdates, count)) {
        return qcError(u"%1 is damaged (count)"_s.arg(path));
    }

    QList<QcBlockPos> positions;
    positions.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        qint32 x = 0;
        qint32 y = 0;
        qint32 z = 0;
        stream >> x >> y >> z;

        const QcBlockPos pos{x, y, z};
        if (stream.status() != QDataStream::Ok || !pos.isInsideWorldHeight() || std::abs(x) > QcWorld::maxHorizontalBlock
            || std::abs(z) > QcWorld::maxHorizontalBlock) {
            return qcError(u"%1 is damaged (position %2)"_s.arg(path).arg(i));
        }
        positions.append(pos);
    }

    if (!stream.atEnd()) {
        return qcError(u"%1 is damaged"_s.arg(path));
    }
    return positions;
}
