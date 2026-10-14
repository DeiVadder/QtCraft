#include "QcEntityStorage.h"

#include "QcArmour.h"
#include "QcArmourStandManager.h"
#include "QcFallingBlockManager.h"
#include "QcNameText.h"
#include "QcPhysics.h"
#include "QcProtocolCodec.h"
#include "QcRegionStorage.h"
#include "QcSaveFile.h"
#include "QcStreamLimits.h"
#include "QcWorldConstants.h"

#include <QIODevice>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr QByteArrayView magic("QCER");
// Version 2: the order keys are 64 bits (entity ids, decision E-366); version 1 files of older
// development versions are refused.
constexpr quint16 version = 2;
// Longer breeding timers than a day of seconds are damaged data.
constexpr double maxBreedingSeconds = 86400.0;
constexpr double maxSpeed = 60.0;

bool isFinite(const QcVec3 &v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

QcVec3 clampedVelocity(const QcVec3 &velocity)
{
    return {std::clamp(velocity.x, -maxSpeed, maxSpeed), std::clamp(velocity.y, -maxSpeed, maxSpeed), std::clamp(velocity.z, -maxSpeed, maxSpeed)};
}

void writeVec(QDataStream &stream, const QcVec3 &v)
{
    stream << v.x << v.y << v.z;
}

void readVec(QDataStream &stream, QcVec3 &v)
{
    stream >> v.x >> v.y >> v.z;
}

// The warnings of one read: each unknown name is reported once per world, not once per chunk.
struct ReadWarnings
{
    QStringList &list;
    QSet<QString> &unknown;
};

// Reads the entries of one chunk record from a region file and checks them: every entity lies in
// the record's chunk, at a finite place inside the world.
class RecordReader
{
public:
    RecordReader(QDataStream &stream, const QcContentRegistry &content, const QString &path, ReadWarnings warnings, QcEntityRecord &record)
        : m_stream(stream)
        , m_content(content)
        , m_path(path)
        , m_warnings(warnings.list)
        , m_unknown(warnings.unknown)
        , m_record(record)
    {
    }

    bool read()
    {
        return entries([this] { return creature(); }) && entries([this] { return item(); }) && entries([this] { return falling(); })
               && entries([this] { return stand(); });
    }

private:
    template<typename Read>
    bool entries(Read read)
    {
        quint32 count = 0;
        if (!QcStreamLimits::readCount(m_stream, QcEntityStorage::maxPerChunk, count)) {
            return false;
        }
        for (quint32 i = 0; i < count; ++i) {
            if (!read()) {
                return false;
            }
        }
        return true;
    }

    // A finite place in this record's chunk, with room above and below the world.
    bool inChunk(const QcVec3 &feet) const
    {
        return isFinite(feet) && std::abs(feet.x) <= QcWorld::maxHorizontalBlock && std::abs(feet.z) <= QcWorld::maxHorizontalBlock
               && std::abs(feet.y) <= 4.0 * QcWorld::worldHeight && QcPhysics::blockContaining(feet).chunk() == m_record.chunk;
    }

    void warnOnce(const QString &kind, const QString &name)
    {
        if (!m_unknown.contains(kind + name)) {
            m_unknown.insert(kind + name);
            m_warnings.append(u"%1: unknown %2 '%3' dropped (in this world reported only here)"_s.arg(m_path, kind, name));
        }
    }

    bool creature()
    {
        QString name;
        QcEntityRecord::Ordered<QcMobManager::Stored> entry;
        QcMobManager::Stored &mob = entry.entry;
        m_stream >> name;
        readVec(m_stream, mob.position);
        m_stream >> mob.yaw >> mob.health >> mob.breedCooldown >> mob.growUp >> mob.bred >> entry.order;
        const bool finite = std::isfinite(mob.yaw) && std::isfinite(mob.health) && std::isfinite(mob.breedCooldown) && std::isfinite(mob.growUp);
        if (m_stream.status() != QDataStream::Ok || !finite || !inChunk(mob.position) || mob.breedCooldown > maxBreedingSeconds
            || mob.growUp > maxBreedingSeconds) {
            return false;
        }

        const std::optional<int> type = m_content.mobIndex(name);
        if (!type) {
            warnOnce(u"creature"_s, name);
            return true;
        }
        mob.type = *type;
        mob.yaw = std::fmod(mob.yaw, 360.0f);
        m_record.creatures.append(entry);
        return true;
    }

    bool item()
    {
        QString name;
        qint32 count = 0;
        qint32 damage = 0;
        QcEntityRecord::Ordered<QcItemEntityManager::Drop> entry;
        QcItemEntityManager::Drop &drop = entry.entry;
        m_stream >> name >> count >> damage;
        readVec(m_stream, drop.position);
        readVec(m_stream, drop.velocity);
        m_stream >> drop.age >> drop.pickupDelay >> entry.order;
        if (m_stream.status() != QDataStream::Ok || !inChunk(drop.position) || !isFinite(drop.velocity) || !std::isfinite(drop.age)
            || !std::isfinite(drop.pickupDelay)) {
            return false;
        }

        const std::optional<QcItemId> item = name.isEmpty() ? std::nullopt : m_content.itemId(name);
        if (!item) {
            if (!name.isEmpty()) {
                warnOnce(u"item"_s, name);
            }
            return true;
        }
        addItem(entry, *item, count, damage);
        return true;
    }

    // A stack saved larger than the item allows now (a mod lowered maxStack) is split.
    void addItem(const QcEntityRecord::Ordered<QcItemEntityManager::Drop> &read, QcItemId item, int count, int damage)
    {
        const QcItemDefinition &definition = m_content.item(item);
        QcEntityRecord::Ordered<QcItemEntityManager::Drop> entry = read;
        QcItemEntityManager::Drop &drop = entry.entry;
        drop.velocity = clampedVelocity(drop.velocity);
        drop.age = std::clamp(drop.age, 0.0, QcItemEntityManager::despawnSeconds);
        drop.pickupDelay = std::clamp(drop.pickupDelay, 0.0, QcItemEntityManager::thrownPickupDelay);
        int left = std::clamp(count, 1, QcItemDefinition::maxStackLimit);
        while (left > 0) {
            drop.stack = {item, std::min(left, definition.maxStack), std::clamp(damage, 0, std::max(0, definition.durability - 1))};
            left -= drop.stack.count;
            m_record.items.append(entry);
        }
    }

    bool falling()
    {
        QString name;
        QcEntityRecord::Ordered<QcFallingBlock> entry;
        QcFallingBlock &block = entry.entry;
        m_stream >> name;
        readVec(m_stream, block.motion.position);
        readVec(m_stream, block.motion.velocity);
        m_stream >> block.age >> entry.order;
        if (m_stream.status() != QDataStream::Ok || !inChunk(block.motion.position) || !isFinite(block.motion.velocity) || !std::isfinite(block.age)) {
            return false;
        }

        QStringList problems;
        const std::optional<QcBlockId> state = m_content.stateId(name, &problems);
        if (!state || *state == qcAirBlockId) {
            warnOnce(u"falling block"_s, name);
            return true;
        }
        m_warnings.append(problems);
        block.motion.velocity = clampedVelocity(block.motion.velocity);
        block.age = std::clamp(block.age, 0.0, QcFallingBlockManager::maxSeconds);
        block.block = *state;
        m_record.fallingBlocks.append(entry);
        return true;
    }

    bool stand()
    {
        QcEntityRecord::Ordered<QcArmourStand> entry;
        QcArmourStand &stand = entry.entry;
        readVec(m_stream, stand.motion.position);
        m_stream >> stand.yaw;
        stand.pose = QcProtocolCodec::readStandPose(m_stream);
        m_stream >> stand.name >> stand.item;
        stand.held = standStack();
        for (QcItemStack &stack : stand.equipment.stacks) {
            stack = standStack();
        }
        m_stream >> stand.cape >> entry.order;
        const double y = stand.motion.position.y;
        if (m_stream.status() != QDataStream::Ok || !inChunk(stand.motion.position) || y < QcWorld::voidLimitY || y > QcWorld::worldHeight + 64.0
            || !std::isfinite(stand.yaw)) {
            return false;
        }

        stand.yaw = qcNormalizedAngle(stand.yaw);
        stand.name = QcNameText::clean(stand.name, QcArmourStandManager::maxNameLength);
        fitArmour(stand);
        checkCape(stand);
        m_record.stands.append(entry);
        return true;
    }

    // What a stand wears or holds: one item, by name; unknown items are dropped with a warning.
    QcItemStack standStack()
    {
        QString name;
        qint32 count = 0;
        qint32 damage = 0;
        m_stream >> name >> count >> damage;
        if (name.isEmpty() || count <= 0) {
            return {};
        }

        const std::optional<QcItemId> item = m_content.itemId(name);
        if (!item) {
            warnOnce(u"item"_s, name);
            return {};
        }
        const QcItemDefinition &definition = m_content.item(*item);
        return {*item, 1, std::clamp(int(damage), 0, std::max(0, definition.durability - 1))};
    }

    void fitArmour(QcArmourStand &stand)
    {
        for (std::size_t slot = 0; slot < QcEquipment::slotCount; ++slot) {
            QcItemStack &stack = stand.equipment.stacks[slot];
            if (!stack.isEmpty() && !QcArmour::fits(m_content, stack, QcEquipmentSlot(slot))) {
                m_warnings.append(u"%1: '%2' is no armour for its slot on a stand; dropped"_s.arg(m_path, m_content.item(stack.item).name));
                stack = {};
            }
        }
    }

    void checkCape(QcArmourStand &stand)
    {
        const QcAppearanceDefinition *cape = stand.cape.isEmpty() ? nullptr : m_content.appearance(stand.cape);
        if (!stand.cape.isEmpty() && (!cape || cape->kind != QcAppearanceDefinition::Kind::Cape)) {
            warnOnce(u"cape on a stand"_s, stand.cape);
            stand.cape.clear();
        }
    }

    QDataStream &m_stream;
    const QcContentRegistry &m_content;
    const QString &m_path;
    QStringList &m_warnings;
    QSet<QString> &m_unknown;
    QcEntityRecord &m_record;
};

} // namespace

QcEntityStorage::QcEntityStorage(QString directory, std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
    , m_files(std::move(directory), QString::fromLatin1(folder), u"qen"_s, magic, version)
{
}

void QcEntityStorage::writeRecord(QDataStream &stream, const QcEntityRecord &record) const
{
    stream << qint32(record.chunk.x) << qint32(record.chunk.z);
    stream << quint32(record.creatures.size());
    for (const auto &[mob, order] : record.creatures) {
        stream << m_content->mobs()[mob.type].name;
        writeVec(stream, mob.position);
        stream << mob.yaw << mob.health << mob.breedCooldown << mob.growUp << mob.bred << order;
    }
    stream << quint32(record.items.size());
    for (const auto &[drop, order] : record.items) {
        QcSaveFile::writeStack(stream, drop.stack, *m_content);
        writeVec(stream, drop.position);
        writeVec(stream, drop.velocity);
        stream << drop.age << drop.pickupDelay << order;
    }
    stream << quint32(record.fallingBlocks.size());
    for (const auto &[falling, order] : record.fallingBlocks) {
        stream << m_content->stateName(falling.block);
        writeVec(stream, falling.motion.position);
        writeVec(stream, falling.motion.velocity);
        stream << falling.age << order;
    }
    stream << quint32(record.stands.size());
    for (const auto &[stand, order] : record.stands) {
        writeVec(stream, stand.motion.position);
        stream << stand.yaw;
        QcProtocolCodec::writeStandPose(stream, stand.pose);
        stream << stand.name << stand.item;
        QcSaveFile::writeStack(stream, stand.held, *m_content);
        for (const QcItemStack &stack : stand.equipment.stacks) {
            QcSaveFile::writeStack(stream, stack, *m_content);
        }
        stream << stand.cape << order;
    }
}

QByteArray QcEntityStorage::payloadOf(const QList<const QcEntityRecord *> &records, QList<QcChunkPos> populated) const
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    stream << quint32(records.size());
    for (const QcEntityRecord *record : records) {
        writeRecord(stream, *record);
    }

    // In a fixed order, so an unchanged region gives the same bytes.
    std::ranges::sort(populated, [](QcChunkPos a, QcChunkPos b) { return std::pair{a.x, a.z} < std::pair{b.x, b.z}; });
    stream << quint32(populated.size());
    for (const QcChunkPos &chunk : std::as_const(populated)) {
        stream << qint32(chunk.x) << qint32(chunk.z);
    }
    return payload;
}

QcStatus QcEntityStorage::write(const QcWorldEntityRecords &entities) const
{
    QHash<QcChunkPos, QList<const QcEntityRecord *>> regions;
    for (const QcEntityRecord &record : entities.records) {
        if (!record.isEmpty()) {
            regions[QcRegionStorage::regionOf(record.chunk)].append(&record);
        }
    }
    QHash<QcChunkPos, QList<QcChunkPos>> populated;
    for (const QcChunkPos &chunk : entities.populated) {
        populated[QcRegionStorage::regionOf(chunk)].append(chunk);
        regions[QcRegionStorage::regionOf(chunk)];
    }

    QHash<QcChunkPos, QByteArray> payloads;
    for (auto it = regions.begin(); it != regions.end(); ++it) {
        std::ranges::sort(*it, [](const QcEntityRecord *a, const QcEntityRecord *b) {
            return std::pair{a->chunk.x, a->chunk.z} < std::pair{b->chunk.x, b->chunk.z};
        });
        payloads.insert(it.key(), payloadOf(*it, populated.value(it.key())));
    }
    return m_files.write(payloads);
}

QcResult<QcWorldEntityRecords> QcEntityStorage::read(QStringList &warnings) const
{
    const QcResult<QList<std::pair<QString, QByteArray>>> files = m_files.read();
    if (!files) {
        return qcError(files.error());
    }

    QcWorldEntityRecords entities;
    QSet<QString> unknown;
    for (const auto &[path, payload] : *files) {
        if (const QcStatus read = readRegion(path, payload, entities, warnings, unknown); !read) {
            m_files.noteDamaged(path);
            return qcError(read.error());
        }
    }
    return entities;
}

QcStatus QcEntityStorage::readRegion(const QString &path, const QByteArray &payload, QcWorldEntityRecords &entities, QStringList &warnings,
                                     QSet<QString> &unknown) const
{
    QDataStream stream(payload);
    stream.setVersion(QcSaveFile::streamVersion);
    constexpr quint32 chunksPerRegion = quint32(QcRegionStorage::regionSize * QcRegionStorage::regionSize);
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, chunksPerRegion, count)) {
        return qcError(u"%1 is damaged (chunk count)"_s.arg(path));
    }

    // Every chunk of the file must belong to the region its name says, and only once.
    QSet<QcChunkPos> chunks;
    const auto belongs = [&](QcChunkPos chunk) {
        return std::abs(qint64(chunk.x)) <= QcWorld::maxHorizontalChunk && std::abs(qint64(chunk.z)) <= QcWorld::maxHorizontalChunk
               && m_files.isFileOf(path, QcRegionStorage::regionOf(chunk));
    };
    for (quint32 i = 0; i < count; ++i) {
        QcEntityRecord record;
        qint32 x = 0;
        qint32 z = 0;
        stream >> x >> z;
        record.chunk = {x, z};
        if (stream.status() != QDataStream::Ok || !belongs(record.chunk) || chunks.contains(record.chunk)
            || !RecordReader(stream, *m_content, path, {warnings, unknown}, record).read()) {
            return qcError(u"%1 is damaged (chunk %2)"_s.arg(path).arg(i));
        }
        chunks.insert(record.chunk);
        entities.records.append(std::move(record));
    }

    if (!QcStreamLimits::readCount(stream, chunksPerRegion, count)) {
        return qcError(u"%1 is damaged (populated chunks)"_s.arg(path));
    }
    std::optional<QcChunkPos> region;
    for (quint32 i = 0; i < count; ++i) {
        qint32 x = 0;
        qint32 z = 0;
        stream >> x >> z;
        if (stream.status() != QDataStream::Ok || !belongs({x, z})) {
            return qcError(u"%1 is damaged (populated chunk %2)"_s.arg(path).arg(i));
        }
        entities.populated.insert({x, z});
        region = QcRegionStorage::regionOf({x, z});
    }
    if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
        return qcError(u"%1 is damaged"_s.arg(path));
    }

    if (!chunks.isEmpty()) {
        region = QcRegionStorage::regionOf(*chunks.cbegin());
    }
    if (region) {
        m_files.remember(*region, payload);
    }
    return {};
}
