#include "QcWorldStorage.h"

#include "QcCrc32.h"
#include "QcProtocol.h"
#include "QcStreamLimits.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr QDataStream::Version streamVersion = QDataStream::Qt_6_8;
// Format versions per file type; readers accept every version up to the current one.
constexpr quint16 baseVersion = 1;
// Version 2 adds food and exhaustion.
constexpr quint16 playerVersion = 2;
constexpr quint16 machinesVersion = 1;
constexpr qint64 maxFileBytes = qint64(256) * 1024 * 1024;
constexpr QByteArrayView regionMagic("QCRG");
constexpr QByteArrayView playerMagic("QCPL");
constexpr QByteArrayView entitiesMagic("QCEN");
constexpr QByteArrayView machinesMagic("QCMA");
// Far more than the item manager keeps, so a valid file is never cut short.
constexpr quint32 maxStoredEntities = 65536;

// [magic][quint16 version][quint32 payload size][quint32 crc32][payload]
QcStatus writeContainer(const QString &path, QByteArrayView magic, const QByteArray &payload, quint16 version = baseVersion)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return qcError(u"Cannot write %1: %2"_s.arg(path, file.errorString()));
    }
    QDataStream stream(&file);
    stream.setVersion(streamVersion);
    stream.writeRawData(magic.data(), int(magic.size()));
    stream << version << quint32(payload.size()) << qcCrc32(payload);
    stream.writeRawData(payload.constData(), int(payload.size()));
    if (stream.status() != QDataStream::Ok || !file.commit()) {
        return qcError(u"Cannot write %1: %2"_s.arg(path, file.errorString()));
    }
    return {};
}

// `version` receives the file's format version, at most `newestVersion`.
QcResult<QByteArray> readContainer(const QString &path, QByteArrayView magic, quint16 newestVersion = baseVersion,
                                   quint16 *version = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"Cannot open %1: %2"_s.arg(path, file.errorString()));
    }
    if (file.size() > maxFileBytes) {
        return qcError(u"%1 is too large"_s.arg(path));
    }
    const QByteArray data = file.readAll();
    constexpr qsizetype headerSize = 4 + 2 + 4 + 4;
    if (data.size() < headerSize || !data.startsWith(magic)) {
        return qcError(u"%1 is not a QtCraft save file or is damaged"_s.arg(path));
    }
    QDataStream stream(data);
    stream.setVersion(streamVersion);
    stream.skipRawData(4);
    quint16 fileVersion = 0;
    quint32 size = 0;
    quint32 crc = 0;
    stream >> fileVersion >> size >> crc;
    if (fileVersion < baseVersion || fileVersion > newestVersion) {
        return qcError(u"%1 has unsupported format version %2"_s.arg(path).arg(fileVersion));
    }
    if (version) {
        *version = fileVersion;
    }
    if (qsizetype(size) != data.size() - headerSize) {
        return qcError(u"%1 is truncated or damaged"_s.arg(path));
    }
    QByteArray payload = data.mid(headerSize);
    if (qcCrc32(payload) != crc) {
        return qcError(u"%1 is damaged (checksum mismatch)"_s.arg(path));
    }
    return payload;
}

QString sanitizedPlayerName(const QString &name)
{
    QString result;
    for (const QChar c : name) {
        result.append(c.isLetterOrNumber() || c == u'_' || c == u'-' ? c : u'_');
    }
    return result.isEmpty() ? u"player"_s : result.left(32);
}

void writeStack(QDataStream &stream, QcItemStack stack, const QcContentRegistry &content)
{
    const QString name = !stack.isEmpty() && content.isValidItem(stack.item) ? content.item(stack.item).name : QString();
    stream << name << qint32(name.isEmpty() ? 0 : stack.count) << qint32(stack.damage);
}

} // namespace

QcWorldStorage::QcWorldStorage(QString directory, std::shared_ptr<const QcContentRegistry> content)
    : m_directory(std::move(directory))
    , m_content(std::move(content))
{
}

QcChunkPos QcWorldStorage::regionOf(QcChunkPos chunk)
{
    return {qcFloorDiv(chunk.x, regionSize), qcFloorDiv(chunk.z, regionSize)};
}

QString QcWorldStorage::regionPath(QcChunkPos region) const
{
    return QDir(m_directory).filePath(u"regions/r.%1.%2.qcr"_s.arg(region.x).arg(region.z));
}

QString QcWorldStorage::playerPath(const QString &playerName) const
{
    return QDir(m_directory).filePath(u"players/%1.qcp"_s.arg(sanitizedPlayerName(playerName)));
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
    if (json.value(u"formatVersion"_s).toInt() != QcWorldInfo::formatVersion) {
        return qcError(u"%1 has unsupported format version %2"_s.arg(path).arg(json.value(u"formatVersion"_s).toInt()));
    }
    bool seedOk = false;
    QcWorldInfo info;
    info.name = json.value(u"name"_s).toString();
    info.seed = json.value(u"seed"_s).toString().toLongLong(&seedOk);
    info.created = QDateTime::fromString(json.value(u"created"_s).toString(), Qt::ISODate);
    info.lastPlayed = QDateTime::fromString(json.value(u"lastPlayed"_s).toString(), Qt::ISODate);
    info.worldTime = std::max(0.0, json.value(u"worldTime"_s).toDouble());
    info.keepInventory = json.value(u"gameRules"_s).toObject().value(u"keepInventory"_s).toBool(true);
    const QJsonArray packs = json.value(u"packs"_s).toArray();
    for (const auto &entry : packs) {
        const QJsonObject pack = entry.toObject();
        if (info.packs.size() < 1024 && !pack.value(u"id"_s).toString().isEmpty()) {
            info.packs.append({pack.value(u"id"_s).toString(), pack.value(u"name"_s).toString(), pack.value(u"version"_s).toString()});
        }
    }
    if (!seedOk || info.name.isEmpty()) {
        return qcError(u"%1 is damaged: missing name or seed"_s.arg(path));
    }
    return info;
}

QcStatus QcWorldStorage::writeInfo(const QcWorldInfo &info) const
{
    if (!QDir().mkpath(m_directory)) {
        return qcError(u"Cannot create %1"_s.arg(m_directory));
    }
    QJsonArray packs;
    for (const QcPackReference &pack : info.packs) {
        packs.append(QJsonObject{{u"id"_s, pack.id}, {u"name"_s, pack.name}, {u"version"_s, pack.version}});
    }
    const QJsonObject json{{u"formatVersion"_s, QcWorldInfo::formatVersion},
                           {u"packs"_s, packs},
                           {u"name"_s, info.name},
                           // As a string: JSON numbers cannot hold every 64-bit seed exactly.
                           {u"seed"_s, QString::number(info.seed)},
                           {u"created"_s, info.created.toString(Qt::ISODate)},
                           {u"lastPlayed"_s, info.lastPlayed.toString(Qt::ISODate)},
                           {u"worldTime"_s, info.worldTime},
                           {u"gameRules"_s, QJsonObject{{u"keepInventory"_s, info.keepInventory}}}};
    QSaveFile file(QDir(m_directory).filePath(u"world.json"_s));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(json).toJson()) < 0 || !file.commit()) {
        return qcError(u"Cannot write world info: %1"_s.arg(file.errorString()));
    }
    return {};
}

QcStatus QcWorldStorage::writeChunks(const QHash<QcChunkPos, std::shared_ptr<const QcChunk>> &chunks) const
{
    QHash<QcChunkPos, QList<std::shared_ptr<const QcChunk>>> regions;
    for (auto it = chunks.constBegin(); it != chunks.constEnd(); ++it) {
        regions[regionOf(it.key())].append(it.value());
    }
    if (!regions.isEmpty() && !QDir().mkpath(QDir(m_directory).filePath(u"regions"_s))) {
        return qcError(u"Cannot create the regions directory in %1"_s.arg(m_directory));
    }
    for (auto region = regions.constBegin(); region != regions.constEnd(); ++region) {
        // File-local palette of block names; chunk data refers to indices into it.
        QList<QString> names;
        QHash<QcBlockId, QcBlockId> localIds;
        QList<QcChunk> remapped;
        for (const std::shared_ptr<const QcChunk> &chunk : region.value()) {
            QcChunk copy(chunk->pos());
            for (int s = 0; s < QcWorld::sectionCount; ++s) {
                const QcChunkSection &section = chunk->section(s);
                QList<QcBlockId> palette;
                for (const QcBlockId id : section.palette()) {
                    if (!localIds.contains(id)) {
                        localIds.insert(id, QcBlockId(names.size()));
                        names.append(m_content->block(id).name);
                    }
                    palette.append(localIds.value(id));
                }
                if (!copy.section(s).assign(palette, section.indices())) {
                    return qcError(u"Internal error while saving chunk"_s);
                }
            }
            remapped.append(copy);
        }
        QByteArray payload;
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream.setVersion(streamVersion);
        stream << quint32(names.size());
        for (const QString &name : std::as_const(names)) {
            stream << name;
        }
        stream << quint32(remapped.size());
        for (const QcChunk &chunk : std::as_const(remapped)) {
            QcProtocol::writeChunk(stream, chunk);
        }
        if (const QcStatus written = writeContainer(regionPath(region.key()), regionMagic, payload); !written) {
            return written;
        }
    }
    return {};
}

QcResult<QHash<QcChunkPos, std::shared_ptr<QcChunk>>> QcWorldStorage::readChunks(QStringList &warnings) const
{
    QHash<QcChunkPos, std::shared_ptr<QcChunk>> chunks;
    const QDir regions(QDir(m_directory).filePath(u"regions"_s));
    const QStringList files = regions.entryList({u"r.*.qcr"_s}, QDir::Files, QDir::Name);
    for (const QString &fileName : files) {
        const QString path = regions.filePath(fileName);
        const QcResult<QByteArray> payload = readContainer(path, regionMagic);
        if (!payload) {
            return qcError(payload.error());
        }
        QDataStream stream(*payload);
        stream.setVersion(streamVersion);
        quint32 nameCount = 0;
        if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, nameCount)) {
            return qcError(u"%1 is damaged (block palette)"_s.arg(path));
        }
        QList<QcBlockId> runtimeIds;
        for (quint32 i = 0; i < nameCount; ++i) {
            QString name;
            stream >> name;
            const std::optional<QcBlockId> id = m_content->blockId(name);
            if (!id) {
                warnings.append(u"%1: unknown block '%2' replaced by air"_s.arg(fileName, name));
            }
            runtimeIds.append(id.value_or(qcAirBlockId));
        }
        quint32 chunkCount = 0;
        if (!QcStreamLimits::readCount(stream, quint32(regionSize * regionSize), chunkCount)) {
            return qcError(u"%1 is damaged (chunk count)"_s.arg(path));
        }
        for (quint32 i = 0; i < chunkCount; ++i) {
            QcResult<std::shared_ptr<QcChunk>> chunk = QcProtocol::readChunk(stream);
            if (!chunk) {
                return qcError(u"%1: %2"_s.arg(path, chunk.error()));
            }
            for (int s = 0; s < QcWorld::sectionCount; ++s) {
                QcChunkSection &section = (*chunk)->section(s);
                QList<QcBlockId> palette;
                for (const QcBlockId local : section.palette()) {
                    if (qsizetype(local) >= runtimeIds.size()) {
                        return qcError(u"%1 is damaged (block index)"_s.arg(path));
                    }
                    palette.append(runtimeIds[qsizetype(local)]);
                }
                if (!section.assign(palette, section.indices())) {
                    return qcError(u"%1 is damaged (section)"_s.arg(path));
                }
                section.compact();
            }
            const QcChunkPos pos = (*chunk)->pos();
            if (std::abs(qint64(pos.x)) > QcWorld::maxHorizontalChunk || std::abs(qint64(pos.z)) > QcWorld::maxHorizontalChunk
                || QFileInfo(regionPath(regionOf(pos))).fileName() != fileName) {
                return qcError(u"%1 contains a chunk of another region"_s.arg(path));
            }
            if (chunks.contains(pos)) {
                return qcError(u"%1 contains chunk %2, %3 twice"_s.arg(path).arg(pos.x).arg(pos.z));
            }
            chunks.insert((*chunk)->pos(), *chunk);
        }
        if (!stream.atEnd()) {
            return qcError(u"%1 is damaged (trailing data)"_s.arg(path));
        }
    }
    return chunks;
}

QcStatus QcWorldStorage::writePlayer(const QString &playerName, const QcPlayerData &player) const
{
    if (!QDir().mkpath(QDir(m_directory).filePath(u"players"_s))) {
        return qcError(u"Cannot create the players directory in %1"_s.arg(m_directory));
    }
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(streamVersion);
    const QcEntityMotion &m = player.motion;
    stream << m.position.x << m.position.y << m.position.z << m.velocity.x << m.velocity.y << m.velocity.z << m.onGround
           << player.yaw << player.pitch << player.health << qint32(player.inventory.selectedHotbarSlot())
           << quint32(QcInventory::slotCount);
    for (int i = 0; i < QcInventory::slotCount; ++i) {
        writeStack(stream, player.inventory.slot(i), *m_content);
    }
    stream << qint32(player.food) << player.exhaustion;
    return writeContainer(playerPath(playerName), playerMagic, payload, playerVersion);
}

QcResult<std::optional<QcPlayerData>> QcWorldStorage::readPlayer(const QString &playerName, QStringList &warnings) const
{
    const QString path = playerPath(playerName);
    if (!QFile::exists(path)) {
        return std::optional<QcPlayerData>();
    }
    quint16 version = 0;
    const QcResult<QByteArray> payload = readContainer(path, playerMagic, playerVersion, &version);
    if (!payload) {
        return qcError(payload.error());
    }
    QDataStream stream(*payload);
    stream.setVersion(streamVersion);
    QcPlayerData player;
    QcEntityMotion &m = player.motion;
    qint32 selected = 0;
    quint32 slotCount = 0;
    stream >> m.position.x >> m.position.y >> m.position.z >> m.velocity.x >> m.velocity.y >> m.velocity.z >> m.onGround
        >> player.yaw >> player.pitch >> player.health >> selected;
    if (!QcStreamLimits::readCount(stream, QcInventory::slotCount, slotCount)) {
        return qcError(u"%1 is damaged (inventory)"_s.arg(path));
    }
    for (quint32 i = 0; i < slotCount; ++i) {
        QString name;
        qint32 count = 0;
        qint32 damage = 0;
        stream >> name >> count >> damage;
        if (name.isEmpty() || count <= 0) {
            continue;
        }
        const std::optional<QcItemId> item = m_content->itemId(name);
        if (!item) {
            warnings.append(u"%1: unknown item '%2' dropped"_s.arg(path, name));
            continue;
        }
        const QcItemDefinition &definition = m_content->item(*item);
        player.inventory.setSlot(int(i), {*item, std::clamp(count, 1, definition.maxStack),
                                          std::clamp(damage, 0, std::max(0, definition.durability - 1))});
    }
    player.inventory.setSelectedHotbarSlot(selected);
    if (version >= 2) {
        qint32 food = 0;
        stream >> food >> player.exhaustion;
        player.food = std::clamp(food, 0, QcItemDefinition::maxFood);
    }
    const auto finite = [](const QcVec3 &v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    constexpr double horizontalLimit = QcWorld::maxHorizontalBlock;
    constexpr double verticalMargin = 64.0;
    const bool inWorld = finite(m.position) && std::abs(m.position.x) <= horizontalLimit && std::abs(m.position.z) <= horizontalLimit
                         && m.position.y >= -verticalMargin && m.position.y <= QcWorld::worldHeight + verticalMargin;
    const bool directionOk = std::isfinite(player.yaw) && std::isfinite(player.pitch) && std::abs(player.pitch) <= 90.0f;
    if (stream.status() != QDataStream::Ok || !stream.atEnd() || !inWorld || !finite(m.velocity) || !directionOk
        || !std::isfinite(player.health) || !std::isfinite(player.exhaustion)) {
        return qcError(u"%1 is damaged"_s.arg(path));
    }
    player.health = std::clamp(player.health, 0.0, 1000.0);
    player.exhaustion = std::max(0.0, player.exhaustion);
    player.yaw = std::fmod(player.yaw, 360.0f);
    return std::optional<QcPlayerData>(player);
}

QcStatus QcWorldStorage::writeEntities(const QcWorldEntities &entities) const
{
    if (!QDir().mkpath(m_directory)) {
        return qcError(u"Cannot create %1"_s.arg(m_directory));
    }
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(streamVersion);
    stream << quint32(entities.items.size());
    for (const QcItemEntityManager::Drop &item : entities.items) {
        writeStack(stream, item.stack, *m_content);
        stream << item.position.x << item.position.y << item.position.z << item.velocity.x << item.velocity.y
               << item.velocity.z << item.age << item.pickupDelay;
    }
    stream << quint32(entities.saplings.size());
    for (const QcPlantManager::Sapling &sapling : entities.saplings) {
        stream << qint32(sapling.pos.x) << qint32(sapling.pos.y) << qint32(sapling.pos.z) << sapling.remaining;
    }
    return writeContainer(QDir(m_directory).filePath(u"entities.qce"_s), entitiesMagic, payload);
}

QcResult<QcWorldEntities> QcWorldStorage::readEntities(QStringList &warnings) const
{
    const QString path = QDir(m_directory).filePath(u"entities.qce"_s);
    if (!QFile::exists(path)) {
        return QcWorldEntities();
    }
    const QcResult<QByteArray> payload = readContainer(path, entitiesMagic);
    if (!payload) {
        return qcError(payload.error());
    }
    QDataStream stream(*payload);
    stream.setVersion(streamVersion);
    const auto finite = [](const QcVec3 &v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    constexpr double horizontalLimit = QcWorld::maxHorizontalBlock;
    QcWorldEntities entities;
    quint32 itemCount = 0;
    if (!QcStreamLimits::readCount(stream, maxStoredEntities, itemCount)) {
        return qcError(u"%1 is damaged (item count)"_s.arg(path));
    }
    QSet<QString> unknown;
    for (quint32 i = 0; i < itemCount; ++i) {
        QString name;
        qint32 count = 0;
        qint32 damage = 0;
        QcItemEntityManager::Drop drop;
        stream >> name >> count >> damage >> drop.position.x >> drop.position.y >> drop.position.z >> drop.velocity.x
            >> drop.velocity.y >> drop.velocity.z >> drop.age >> drop.pickupDelay;
        if (stream.status() != QDataStream::Ok || !finite(drop.position) || !finite(drop.velocity) || !std::isfinite(drop.age)
            || !std::isfinite(drop.pickupDelay) || std::abs(drop.position.x) > horizontalLimit
            || std::abs(drop.position.z) > horizontalLimit || std::abs(drop.position.y) > 4.0 * QcWorld::worldHeight) {
            return qcError(u"%1 is damaged (item %2)"_s.arg(path).arg(i));
        }
        const std::optional<QcItemId> item = name.isEmpty() ? std::nullopt : m_content->itemId(name);
        if (!item) {
            if (!name.isEmpty() && !unknown.contains(name)) {
                unknown.insert(name);
                warnings.append(u"%1: unknown item '%2' dropped"_s.arg(path, name));
            }
            continue;
        }
        const QcItemDefinition &definition = m_content->item(*item);
        drop.velocity = {std::clamp(drop.velocity.x, -60.0, 60.0), std::clamp(drop.velocity.y, -60.0, 60.0),
                         std::clamp(drop.velocity.z, -60.0, 60.0)};
        drop.age = std::clamp(drop.age, 0.0, QcItemEntityManager::despawnSeconds);
        drop.pickupDelay = std::clamp(drop.pickupDelay, 0.0, QcItemEntityManager::thrownPickupDelay);
        // A stack saved larger than the item allows now (e.g. after a mod lowered maxStack) is split.
        int left = std::clamp(count, 1, QcItemDefinition::maxStackLimit);
        while (left > 0 && entities.items.size() < qsizetype(maxStoredEntities)) {
            drop.stack = {*item, std::min(left, definition.maxStack), std::clamp(damage, 0, std::max(0, definition.durability - 1))};
            left -= drop.stack.count;
            entities.items.append(drop);
        }
        if (left > 0) {
            warnings.append(u"%1: too many dropped items, %2 x '%3' not loaded"_s.arg(path).arg(left).arg(name));
        }
    }
    quint32 saplingCount = 0;
    if (!QcStreamLimits::readCount(stream, maxStoredEntities, saplingCount)) {
        return qcError(u"%1 is damaged (sapling count)"_s.arg(path));
    }
    for (quint32 i = 0; i < saplingCount; ++i) {
        qint32 x = 0;
        qint32 y = 0;
        qint32 z = 0;
        double remaining = 0.0;
        stream >> x >> y >> z >> remaining;
        const QcBlockPos pos{x, y, z};
        if (stream.status() != QDataStream::Ok || !std::isfinite(remaining) || !pos.isInsideWorldHeight()
            || std::abs(x) > QcWorld::maxHorizontalBlock || std::abs(z) > QcWorld::maxHorizontalBlock) {
            return qcError(u"%1 is damaged (sapling %2)"_s.arg(path).arg(i));
        }
        entities.saplings.append({pos, std::clamp(remaining, 0.0, 86400.0)});
    }
    if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
        return qcError(u"%1 is damaged"_s.arg(path));
    }
    return entities;
}

QcStatus QcWorldStorage::writeMachines(const QList<QcMachineManager::Machine> &machines) const
{
    if (!QDir().mkpath(m_directory)) {
        return qcError(u"Cannot create %1"_s.arg(m_directory));
    }
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(streamVersion);
    stream << quint32(machines.size());
    for (const QcMachineManager::Machine &machine : machines) {
        stream << qint32(machine.pos.x) << qint32(machine.pos.y) << qint32(machine.pos.z);
        for (const QcItemStack &stack : machine.stacks) {
            writeStack(stream, stack, *m_content);
        }
        stream << machine.burnRemaining << machine.burnSeconds << machine.progress;
    }
    return writeContainer(QDir(m_directory).filePath(u"machines.qce"_s), machinesMagic, payload, machinesVersion);
}

QcResult<QList<QcMachineManager::Machine>> QcWorldStorage::readMachines(QStringList &warnings) const
{
    const QString path = QDir(m_directory).filePath(u"machines.qce"_s);
    if (!QFile::exists(path)) {
        return QList<QcMachineManager::Machine>();
    }
    const QcResult<QByteArray> payload = readContainer(path, machinesMagic, machinesVersion);
    if (!payload) {
        return qcError(payload.error());
    }
    QDataStream stream(*payload);
    stream.setVersion(streamVersion);
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, QcMachineManager::maxMachines, count)) {
        return qcError(u"%1 is damaged (machine count)"_s.arg(path));
    }
    QList<QcMachineManager::Machine> machines;
    QSet<QcBlockPos> positions;
    QSet<QString> unknown;
    for (quint32 i = 0; i < count; ++i) {
        qint32 x = 0;
        qint32 y = 0;
        qint32 z = 0;
        stream >> x >> y >> z;
        QcMachineManager::Machine machine;
        machine.pos = {x, y, z};
        for (QcItemStack &stack : machine.stacks) {
            QString name;
            qint32 itemCount = 0;
            qint32 damage = 0;
            stream >> name >> itemCount >> damage;
            if (name.isEmpty() || itemCount <= 0) {
                continue;
            }
            const std::optional<QcItemId> item = m_content->itemId(name);
            if (!item) {
                if (!unknown.contains(name)) {
                    unknown.insert(name);
                    warnings.append(u"%1: unknown item '%2' dropped"_s.arg(path, name));
                }
                continue;
            }
            const QcItemDefinition &definition = m_content->item(*item);
            stack = {*item, std::clamp(itemCount, 1, definition.maxStack), std::clamp(damage, 0, std::max(0, definition.durability - 1))};
        }
        stream >> machine.burnRemaining >> machine.burnSeconds >> machine.progress;
        const bool timesOk = std::isfinite(machine.burnRemaining) && std::isfinite(machine.burnSeconds)
                             && std::isfinite(machine.progress);
        if (stream.status() != QDataStream::Ok || !timesOk || !machine.pos.isInsideWorldHeight()
            || std::abs(x) > QcWorld::maxHorizontalBlock || std::abs(z) > QcWorld::maxHorizontalBlock || positions.contains(machine.pos)) {
            return qcError(u"%1 is damaged (machine %2)"_s.arg(path).arg(i));
        }
        constexpr double maxSeconds = 100000.0;
        machine.burnSeconds = std::clamp(machine.burnSeconds, 0.0, maxSeconds);
        machine.burnRemaining = std::clamp(machine.burnRemaining, 0.0, machine.burnSeconds);
        machine.progress = std::clamp(machine.progress, 0.0, maxSeconds);
        positions.insert(machine.pos);
        machines.append(machine);
    }
    if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
        return qcError(u"%1 is damaged"_s.arg(path));
    }
    return machines;
}
