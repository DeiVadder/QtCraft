#include "QcEntityStorage.h"

#include "QcSaveFile.h"
#include "QcStreamLimits.h"

#include <QDir>
#include <QFile>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr QByteArrayView entitiesMagic("QCEN");
// Version 3 (stage 31.1) has no saplings any more: they are block entities saved with their chunk
// (decision E-358). Files of older development versions are refused like their worlds.
constexpr quint16 entitiesVersion = 3;
// Version 2 adds each creature's breeding cooldown, time until it grows up and whether it was bred
// (decision E-293).
constexpr quint16 mobsVersion = 2;
// Longer timers than a day of seconds are damaged data.
constexpr double maxBreedingSeconds = 86400.0;
constexpr QByteArrayView mobsMagic("QCMB");
constexpr double horizontalLimit = QcWorld::maxHorizontalBlock;

bool isFinite(const QcVec3 &v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// A finite position inside the world (with room above and below) and a finite velocity.
bool isValidMotion(const QcVec3 &position, const QcVec3 &velocity)
{
    return isFinite(position) && isFinite(velocity) && std::abs(position.x) <= horizontalLimit && std::abs(position.z) <= horizontalLimit
           && std::abs(position.y) <= 4.0 * QcWorld::worldHeight;
}

QcVec3 clampedVelocity(const QcVec3 &velocity)
{
    return {std::clamp(velocity.x, -60.0, 60.0), std::clamp(velocity.y, -60.0, 60.0), std::clamp(velocity.z, -60.0, 60.0)};
}

} // namespace

QcEntityStorage::QcEntityStorage(QString directory, std::shared_ptr<const QcContentRegistry> content)
    : m_directory(std::move(directory))
    , m_content(std::move(content))
{
}

QcStatus QcEntityStorage::writeEntities(const QcWorldEntities &entities) const
{
    if (!QDir().mkpath(m_directory)) {
        return qcError(u"Cannot create %1"_s.arg(m_directory));
    }

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    stream << quint32(entities.items.size());
    for (const QcItemEntityManager::Drop &item : entities.items) {
        QcSaveFile::writeStack(stream, item.stack, *m_content);
        stream << item.position.x << item.position.y << item.position.z << item.velocity.x << item.velocity.y
               << item.velocity.z << item.age << item.pickupDelay;
    }
    stream << quint32(entities.fallingBlocks.size());
    for (const QcFallingBlockManager::Falling &falling : entities.fallingBlocks) {
        const QcEntityMotion &motion = falling.motion;
        stream << m_content->stateName(falling.block) << motion.position.x << motion.position.y << motion.position.z
               << motion.velocity.x << motion.velocity.y << motion.velocity.z << falling.age;
    }
    return QcSaveFile::write(QDir(m_directory).filePath(u"entities.qce"_s), entitiesMagic, payload, entitiesVersion);
}

QcResult<QcWorldEntities> QcEntityStorage::readEntities(QStringList &warnings) const
{
    const QString path = QDir(m_directory).filePath(u"entities.qce"_s);
    if (!QFile::exists(path)) {
        return QcWorldEntities();
    }
    quint16 version = 0;
    const QcResult<QByteArray> payload = QcSaveFile::read(path, entitiesMagic, entitiesVersion, &version);
    if (!payload) {
        return qcError(payload.error());
    }
    if (version < entitiesVersion) {
        return qcError(u"%1 is from an older development version"_s.arg(path));
    }

    QDataStream stream(*payload);
    stream.setVersion(QcSaveFile::streamVersion);
    QcWorldEntities entities;
    if (const QcStatus read = readItems(stream, path, entities.items, warnings); !read) {
        return qcError(read.error());
    }
    if (const QcStatus read = readFallingBlocks(stream, path, entities.fallingBlocks, warnings); !read) {
        return qcError(read.error());
    }
    if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
        return qcError(u"%1 is damaged"_s.arg(path));
    }
    return entities;
}

QcStatus QcEntityStorage::readItems(QDataStream &stream, const QString &path, QList<QcItemEntityManager::Drop> &items,
                                    QStringList &warnings) const
{
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
        if (stream.status() != QDataStream::Ok || !isValidMotion(drop.position, drop.velocity) || !std::isfinite(drop.age)
            || !std::isfinite(drop.pickupDelay)) {
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

        if (const int left = addItem(drop, *item, count, damage, items); left > 0) {
            warnings.append(u"%1: too many dropped items, %2 x '%3' not loaded"_s.arg(path).arg(left).arg(name));
        }
    }
    return {};
}

int QcEntityStorage::addItem(QcItemEntityManager::Drop drop, QcItemId item, int count, int damage,
                             QList<QcItemEntityManager::Drop> &items) const
{
    const QcItemDefinition &definition = m_content->item(item);
    drop.velocity = clampedVelocity(drop.velocity);
    drop.age = std::clamp(drop.age, 0.0, QcItemEntityManager::despawnSeconds);
    drop.pickupDelay = std::clamp(drop.pickupDelay, 0.0, QcItemEntityManager::thrownPickupDelay);
    // A stack saved larger than the item allows now (e.g. after a mod lowered maxStack) is split.
    int left = std::clamp(count, 1, QcItemDefinition::maxStackLimit);
    while (left > 0 && items.size() < qsizetype(maxStoredEntities)) {
        drop.stack = {item, std::min(left, definition.maxStack), std::clamp(damage, 0, std::max(0, definition.durability - 1))};
        left -= drop.stack.count;
        items.append(drop);
    }
    return left;
}

QcStatus QcEntityStorage::readFallingBlocks(QDataStream &stream, const QString &path,
                                            QList<QcFallingBlockManager::Falling> &blocks, QStringList &warnings) const
{
    quint32 fallingCount = 0;
    if (!QcStreamLimits::readCount(stream, maxStoredEntities, fallingCount)) {
        return qcError(u"%1 is damaged (falling block count)"_s.arg(path));
    }

    for (quint32 i = 0; i < fallingCount; ++i) {
        QString name;
        QcFallingBlockManager::Falling falling;
        QcEntityMotion &motion = falling.motion;
        stream >> name >> motion.position.x >> motion.position.y >> motion.position.z >> motion.velocity.x >> motion.velocity.y
            >> motion.velocity.z >> falling.age;
        if (stream.status() != QDataStream::Ok || !isValidMotion(motion.position, motion.velocity) || !std::isfinite(falling.age)) {
            return qcError(u"%1 is damaged (falling block %2)"_s.arg(path).arg(i));
        }

        QStringList problems;
        const std::optional<QcBlockId> block = m_content->stateId(name, &problems);
        if (!block || *block == qcAirBlockId) {
            warnings.append(u"%1: unknown falling block '%2' dropped"_s.arg(path, name));
            continue;
        }

        warnings.append(problems);
        motion.velocity = clampedVelocity(motion.velocity);
        falling.age = std::clamp(falling.age, 0.0, QcFallingBlockManager::maxSeconds);
        falling.block = *block;
        blocks.append(falling);
    }
    return {};
}

QcStatus QcEntityStorage::writeMobs(const QcWorldMobs &mobs) const
{
    if (!QDir().mkpath(m_directory)) {
        return qcError(u"Cannot create %1"_s.arg(m_directory));
    }
    // The same limits as reading, so a saved world always opens again; what is left out is
    // reported but does not fail the save.
    if (mobs.mobs.size() > qsizetype(maxStoredEntities) || mobs.populatedChunks.size() > qsizetype(maxStoredPopulatedChunks)) {
        qWarning("mobs.qce: keeping %d of %lld creatures and %d of %lld populated chunks", int(maxStoredEntities),
                 qlonglong(mobs.mobs.size()), int(maxStoredPopulatedChunks), qlonglong(mobs.populatedChunks.size()));
    }

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    const qsizetype mobCount = std::min(mobs.mobs.size(), qsizetype(maxStoredEntities));
    stream << quint32(mobCount);
    const QList<QcMobManager::Stored> kept = mobs.mobs.first(mobCount);
    for (const QcMobManager::Stored &mob : kept) {
        stream << m_content->mobs()[mob.type].name << mob.position.x << mob.position.y << mob.position.z << mob.yaw << mob.health
               << mob.breedCooldown << mob.growUp << mob.bred;
    }

    const qsizetype chunkCount = std::min(mobs.populatedChunks.size(), qsizetype(maxStoredPopulatedChunks));
    stream << quint32(chunkCount);
    qsizetype written = 0;
    for (auto it = mobs.populatedChunks.cbegin(); it != mobs.populatedChunks.cend() && written < chunkCount; ++it, ++written) {
        stream << qint32(it->x) << qint32(it->z);
    }
    return QcSaveFile::write(QDir(m_directory).filePath(u"mobs.qce"_s), mobsMagic, payload, mobsVersion);
}

QcResult<QcWorldMobs> QcEntityStorage::readMobs(QStringList &warnings) const
{
    const QString path = QDir(m_directory).filePath(u"mobs.qce"_s);
    if (!QFile::exists(path)) {
        return QcWorldMobs();
    }
    quint16 version = 0;
    const QcResult<QByteArray> payload = QcSaveFile::read(path, mobsMagic, mobsVersion, &version);
    if (!payload) {
        return qcError(payload.error());
    }

    QDataStream stream(*payload);
    stream.setVersion(QcSaveFile::streamVersion);
    QcWorldMobs result;
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, maxStoredEntities, count)) {
        return qcError(u"%1 is damaged (creature count)"_s.arg(path));
    }

    QSet<QString> unknown;
    for (quint32 i = 0; i < count; ++i) {
        QString name;
        QcMobManager::Stored mob;
        stream >> name >> mob.position.x >> mob.position.y >> mob.position.z >> mob.yaw >> mob.health;
        if (version >= 2) {
            stream >> mob.breedCooldown >> mob.growUp >> mob.bred;
        }
        const bool finite = std::isfinite(mob.position.x) && std::isfinite(mob.position.y) && std::isfinite(mob.position.z)
                            && std::isfinite(mob.yaw) && std::isfinite(mob.health) && std::isfinite(mob.breedCooldown)
                            && std::isfinite(mob.growUp) && mob.breedCooldown <= maxBreedingSeconds && mob.growUp <= maxBreedingSeconds;
        if (stream.status() != QDataStream::Ok || !finite || std::abs(mob.position.x) > QcWorld::maxHorizontalBlock
            || std::abs(mob.position.z) > QcWorld::maxHorizontalBlock || std::abs(mob.position.y) > 4.0 * QcWorld::worldHeight) {
            return qcError(u"%1 is damaged (creature %2)"_s.arg(path).arg(i));
        }

        const std::optional<int> type = m_content->mobIndex(name);
        if (!type) {
            if (!unknown.contains(name)) {
                unknown.insert(name);
                warnings.append(u"%1: unknown creature '%2' dropped"_s.arg(path, name));
            }
            continue;
        }

        mob.type = *type;
        mob.yaw = std::fmod(mob.yaw, 360.0f);
        result.mobs.append(mob);
    }

    quint32 chunkCount = 0;
    if (!QcStreamLimits::readCount(stream, maxStoredPopulatedChunks, chunkCount)) {
        return qcError(u"%1 is damaged (chunk count)"_s.arg(path));
    }
    // Eight bytes per chunk: never reserve more than the file can hold.
    const qint64 left = stream.device() ? stream.device()->bytesAvailable() : 0;
    result.populatedChunks.reserve(qsizetype(std::min<qint64>(chunkCount, left / 8)));
    for (quint32 i = 0; i < chunkCount && stream.status() == QDataStream::Ok; ++i) {
        qint32 x = 0;
        qint32 z = 0;
        stream >> x >> z;
        result.populatedChunks.insert({x, z});
    }

    if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
        return qcError(u"%1 is damaged"_s.arg(path));
    }
    return result;
}
