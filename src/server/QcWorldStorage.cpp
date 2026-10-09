#include "QcWorldStorage.h"

#include "QcSaveFile.h"
#include "QcStreamLimits.h"

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

// Version 2 adds food and exhaustion.
constexpr quint16 playerVersion = 2;
constexpr QByteArrayView playerMagic("QCPL");
constexpr QByteArrayView blockUpdatesMagic("QCBU");

QString sanitizedPlayerName(const QString &name)
{
    QString result;
    for (const QChar c : name) {
        result.append(c.isLetterOrNumber() || c == u'_' || c == u'-' ? c : u'_');
    }
    return result.isEmpty() ? u"player"_s : result.left(32);
}

} // namespace

QcWorldStorage::QcWorldStorage(QString directory, std::shared_ptr<const QcContentRegistry> content)
    : m_directory(std::move(directory))
    , m_content(std::move(content))
    , m_regions(m_directory, m_content)
    , m_entities(m_directory, m_content)
    , m_blockEntities(m_directory, m_content)
{
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

QcStatus QcWorldStorage::writePlayer(const QString &playerName, const QcPlayerData &player) const
{
    if (!QDir().mkpath(QDir(m_directory).filePath(u"players"_s))) {
        return qcError(u"Cannot create the players directory in %1"_s.arg(m_directory));
    }
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    const QcEntityMotion &m = player.motion;
    stream << m.position.x << m.position.y << m.position.z << m.velocity.x << m.velocity.y << m.velocity.z << m.onGround
           << player.yaw << player.pitch << player.health << qint32(player.inventory.selectedHotbarSlot())
           << quint32(QcInventory::slotCount);
    for (int i = 0; i < QcInventory::slotCount; ++i) {
        QcSaveFile::writeStack(stream, player.inventory.slot(i), *m_content);
    }
    stream << qint32(player.food) << player.exhaustion;
    return QcSaveFile::write(playerPath(playerName), playerMagic, payload, playerVersion);
}

QcResult<std::optional<QcPlayerData>> QcWorldStorage::readPlayer(const QString &playerName, QStringList &warnings) const
{
    const QString path = playerPath(playerName);
    if (!QFile::exists(path)) {
        return std::optional<QcPlayerData>();
    }
    quint16 version = 0;
    const QcResult<QByteArray> payload = QcSaveFile::read(path, playerMagic, playerVersion, &version);
    if (!payload) {
        return qcError(payload.error());
    }
    QDataStream stream(*payload);
    stream.setVersion(QcSaveFile::streamVersion);
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

QcStatus QcWorldStorage::writeBlockUpdates(const QList<QcBlockPos> &positions) const
{
    if (positions.size() > qsizetype(maxStoredBlockUpdates)) {
        return qcError(u"Too many pending block updates to save (%1, at most %2)"_s.arg(positions.size()).arg(maxStoredBlockUpdates));
    }
    const QString path = QDir(m_directory).filePath(u"blockupdates.qce"_s);
    if (positions.isEmpty()) {
        // No file means no pending updates.
        if (QFile::exists(path) && !QFile::remove(path)) {
            return qcError(u"Cannot remove %1"_s.arg(path));
        }
        return {};
    }
    if (!QDir().mkpath(m_directory)) {
        return qcError(u"Cannot create %1"_s.arg(m_directory));
    }
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    stream << quint32(positions.size());
    for (const QcBlockPos &pos : positions) {
        stream << qint32(pos.x) << qint32(pos.y) << qint32(pos.z);
    }
    return QcSaveFile::write(path, blockUpdatesMagic, payload);
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
