#include "QcEntityCodec.h"

#include "QcArmour.h"
#include "QcArmourStandManager.h"
#include "QcFallingBlockManager.h"
#include "QcNameText.h"
#include "QcPhysics.h"
#include "QcProtocolCodec.h"
#include "QcSaveFile.h"
#include "QcStackReader.h"
#include "QcStreamLimits.h"
#include "QcWorldConstants.h"

#include <QIODevice>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

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
               && entries([this] { return stand(); }) && entries([this] { return minecart(); });
    }

private:
    template<typename Read>
    bool entries(Read read)
    {
        quint32 count = 0;
        if (!QcStreamLimits::readCount(m_stream, QcEntityCodec::maxPerChunk, count)) {
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

    bool minecart()
    {
        QcEntityRecord::Ordered<QcMinecart> entry;
        QcMinecart &cart = entry.entry;
        readVec(m_stream, cart.motion.position);
        readVec(m_stream, cart.motion.velocity);
        m_stream >> cart.yaw >> cart.item >> entry.order;
        // A container cart's slots (decision E-386) and whether a hopper cart pulls.
        quint32 slotCount = 0;
        if (!QcStreamLimits::readCount(m_stream, QcBlockDefinition::maxContainerSlots, slotCount)) {
            return false;
        }
        QcStackReader stacks(m_content, m_path, m_warnings, &m_unknown);
        cart.stacks.resize(slotCount);
        for (QcItemStack &stack : cart.stacks) {
            stack = stacks.read(m_stream);
        }
        m_stream >> cart.enabled;
        const double y = cart.motion.position.y;
        if (m_stream.status() != QDataStream::Ok || !inChunk(cart.motion.position) || !isFinite(cart.motion.velocity) || !std::isfinite(cart.yaw)
            || y < QcWorld::voidLimitY || y > QcWorld::worldHeight + 64.0) {
            return false;
        }

        // A cart whose item a mod took away still rolls, it only drops nothing.
        if (!cart.item.isEmpty() && !m_content.itemId(cart.item)) {
            warnOnce(u"item"_s, cart.item);
            cart.item.clear();
        }
        cart.motion.velocity = clampedVelocity(cart.motion.velocity);
        cart.yaw = qcNormalizedAngle(cart.yaw);
        m_record.minecarts.append(entry);
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

void writeRecord(QDataStream &stream, const QcEntityRecord &record, const QcContentRegistry &content)
{
    stream << quint32(record.creatures.size());
    for (const auto &[mob, order] : record.creatures) {
        stream << content.mobs()[mob.type].name;
        writeVec(stream, mob.position);
        stream << mob.yaw << mob.health << mob.breedCooldown << mob.growUp << mob.bred << order;
    }
    stream << quint32(record.items.size());
    for (const auto &[drop, order] : record.items) {
        QcSaveFile::writeStack(stream, drop.stack, content);
        writeVec(stream, drop.position);
        writeVec(stream, drop.velocity);
        stream << drop.age << drop.pickupDelay << order;
    }
    stream << quint32(record.fallingBlocks.size());
    for (const auto &[falling, order] : record.fallingBlocks) {
        stream << content.stateName(falling.block);
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
        QcSaveFile::writeStack(stream, stand.held, content);
        for (const QcItemStack &stack : stand.equipment.stacks) {
            QcSaveFile::writeStack(stream, stack, content);
        }
        stream << stand.cape << order;
    }
    stream << quint32(record.minecarts.size());
    for (const auto &[cart, order] : record.minecarts) {
        writeVec(stream, cart.motion.position);
        writeVec(stream, cart.motion.velocity);
        stream << cart.yaw << cart.item << order << quint32(cart.stacks.size());
        for (const QcItemStack &stack : cart.stacks) {
            QcSaveFile::writeStack(stream, stack, content);
        }
        stream << cart.enabled;
    }
}

} // namespace

namespace QcEntityCodec {

QByteArray encode(const QcEntityRecord &record, const QcContentRegistry &content)
{
    QByteArray blob;
    QDataStream stream(&blob, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    writeRecord(stream, record, content);
    stream << quint8(record.populated ? 1 : 0);
    return blob;
}

[[nodiscard]] QcResult<QcEntityRecord> decode(const QByteArray &blob, QcChunkPos chunk, const QcContentRegistry &content, QStringList &warnings,
                                QSet<QString> &reported)
{
    QDataStream stream(blob);
    stream.setVersion(QcSaveFile::streamVersion);
    QcEntityRecord record;
    record.chunk = chunk;
    const QString where = u"chunk %1, %2"_s.arg(chunk.x).arg(chunk.z);
    quint8 populated = 0;
    const bool read = RecordReader(stream, content, where, {warnings, reported}, record).read();
    stream >> populated;
    if (!read || stream.status() != QDataStream::Ok || !stream.atEnd() || populated > 1) {
        return qcError(u"damaged entities of chunk %1, %2"_s.arg(chunk.x).arg(chunk.z));
    }
    record.populated = populated == 1;
    return record;
}

} // namespace QcEntityCodec
