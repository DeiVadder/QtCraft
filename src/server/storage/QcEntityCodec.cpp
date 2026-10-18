#include "server/storage/QcEntityCodec.h"

#include "common/core/QcNameText.h"
#include "common/core/QcWorldConstants.h"
#include "common/gameplay/QcArmour.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcProtocolCodec.h"
#include "common/protocol/QcStreamLimits.h"
#include "server/blocks/QcFallingBlockManager.h"
#include "server/gameplay/QcArmourStandManager.h"
#include "server/items/QcExperienceOrbManager.h"
#include "server/storage/QcComponentFrames.h"
#include "server/storage/QcSaveFile.h"
#include "server/storage/QcStackReader.h"
#include "server/storage/QcVillagerCodec.h"

#include <QIODevice>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

using Component = QcEntityCodec::Component;

// The current version of the components of each kind (QcVillagerCodec::version for villagers).
constexpr quint16 kindVersion = 1;
// Longer breeding timers than a day of seconds are damaged data.
constexpr double maxBreedingSeconds = 86400.0;
constexpr double maxSpeed = 60.0;
// Type names (creatures, items, block states) longer than this are damaged data.
constexpr qsizetype maxTypeName = 512;

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

// What every entry starts with (decision E-420): what it is by name (empty where the kind says it
// all), where it is and how it moves, its place in the table of its kind (QcEntityRecord) and its
// persistent id (creatures; 0 for the others). What a kind does not use is kept as read
// (QcEntityKept), so a later version may fill it.
struct Header
{
    QString type;
    QcVec3 position;
    QcVec3 velocity;
    float yaw = 0.0f;
    quint64 order = 0;
    quint64 uid = 0;
};

// Writes an entry: its header, then its components, the known ones by `add(Writer &)`, then `kept`.
template<typename Add>
void writeEntry(QDataStream &stream, const Header &header, const QcEntityComponents &kept, Add add)
{
    stream << header.type;
    writeVec(stream, header.position);
    writeVec(stream, header.velocity);
    stream << header.yaw << header.order << header.uid;
    QcComponentFrames::Writer components(stream, kept);
    add(components);
    components.finish();
}

// A component this version reads, by its id, with its reader (`read(stream, version)`).
QcComponentFrames::Known known(Component id, quint16 version, QcComponentFrames::FieldReader read)
{
    return {quint16(id), version, read};
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
        return entries([this](const Header &h) { return creature(h); }) && entries([this](const Header &h) { return item(h); })
               && entries([this](const Header &h) { return falling(h); }) && entries([this](const Header &h) { return stand(h); })
               && entries([this](const Header &h) { return minecart(h); }) && entries([this](const Header &h) { return orb(h); })
               && entries([this](const Header &h) { return projectile(h); });
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
            Header header;
            if (!readHeader(header) || !read(header)) {
                return false;
            }
        }
        return true;
    }

    bool readHeader(Header &header)
    {
        if (!QcComponentFrames::readText(m_stream, header.type, maxTypeName)) {
            return false;
        }
        readVec(m_stream, header.position);
        readVec(m_stream, header.velocity);
        m_stream >> header.yaw >> header.order >> header.uid;
        return m_stream.status() == QDataStream::Ok && inChunk(header.position) && isFinite(header.velocity) && std::isfinite(header.yaw);
    }

    // A finite place in this record's chunk, with room above and below the world.
    bool inChunk(const QcVec3 &feet) const
    {
        return isFinite(feet) && std::abs(feet.x) <= QcWorld::maxHorizontalBlock && std::abs(feet.z) <= QcWorld::maxHorizontalBlock
               && std::abs(feet.y) <= 4.0 * QcWorld::worldHeight && QcPhysics::blockContaining(feet).chunk() == m_record.chunk;
    }

    // Reads the known components of an entry and keeps the others, those beyond
    // QcComponentFrames::maxKept dropped with a warning; false for damaged data.
    bool readComponents(QcEntityComponents &kept, std::initializer_list<QcComponentFrames::Known> readers)
    {
        int dropped = 0;
        if (!QcComponentFrames::read(m_stream, readers, kept, dropped)) {
            return false;
        }
        if (dropped > 0) {
            const QString text = u"%1: an entity with more than %2 unknown components; %3 of them dropped"_s;
            m_warnings.append(text.arg(m_path).arg(QcComponentFrames::maxKept).arg(dropped));
        }
        return true;
    }

    // Keeps the header fields that the entries of a kind do not use in this version (decision E-420).
    static void keepHeader(const Header &header, QcEntityKept &kept, bool withType)
    {
        kept.uid = header.uid;
        if (withType) {
            kept.type = header.type;
        }
    }

    void warnOnce(const QString &kind, const QString &name)
    {
        if (!m_unknown.contains(kind + name)) {
            m_unknown.insert(kind + name);
            m_warnings.append(u"%1: unknown %2 '%3' dropped (in this world reported only here)"_s.arg(m_path, kind, name));
        }
    }

    bool creature(const Header &header)
    {
        QcEntityRecord::Ordered<QcMobManager::Stored> entry{{}, header.order};
        QcMobManager::Stored &mob = entry.entry;
        mob.position = header.position;
        mob.yaw = std::fmod(header.yaw, 360.0f);
        mob.uid = header.uid;
        const std::optional<int> type = m_content.mobIndex(header.type);
        // Without its creature component it is a grown creature at full health.
        mob.health = type ? m_content.mobs()[*type].maxHealth : 1.0;
        const auto body = [&mob](QDataStream &in, quint16) {
            in >> mob.health >> mob.breedCooldown >> mob.growUp >> mob.bred;
            return true;
        };
        const auto villager = [&mob](QDataStream &in, quint16 version) {
            mob.villager = QcVillagerCodec::read(in, version);
            return mob.villager.has_value();
        };
        QString carried;
        const auto carriedBlock = [&carried, &mob](QDataStream &in, quint16) {
            if (!QcComponentFrames::readText(in, carried, maxTypeName)) {
                return false;
            }
            in >> mob.keptForBlock;
            return true;
        };
        const auto confined = [&mob](QDataStream &, quint16) {
            mob.confined = true;
            return true;
        };
        const auto golem = [&mob](QDataStream &in, quint16) {
            in >> mob.playerBuilt;
            return true;
        };
        if (!readComponents(mob.kept.components,
                            {known(Component::Creature, kindVersion, body), known(Component::Villager, QcVillagerCodec::version, villager),
                             known(Component::Carried, kindVersion, carriedBlock), known(Component::Confined, kindVersion, confined),
                             known(Component::Golem, kindVersion, golem)})) {
            return false;
        }
        // A carried block the content no longer has is lost with a warning.
        if (!carried.isEmpty()) {
            const std::optional<QcBlockId> state = m_content.stateId(carried);
            if (!state) {
                warnOnce(u"block"_s, carried);
            }
            mob.carried = state.value_or(qcAirBlockId);
        }
        const bool finite = std::isfinite(mob.health) && std::isfinite(mob.breedCooldown) && std::isfinite(mob.growUp);
        if (!finite || mob.uid == 0 || mob.breedCooldown > maxBreedingSeconds || mob.growUp > maxBreedingSeconds) {
            return false;
        }

        if (!type) {
            warnOnce(u"creature"_s, header.type);
            return true;
        }
        mob.type = *type;
        m_record.creatures.append(entry);
        return true;
    }

    bool item(const Header &header)
    {
        qint32 count = 1;
        qint32 damage = 0;
        QcEntityRecord::Ordered<QcItemEntityManager::Drop> entry{{}, header.order};
        QcItemEntityManager::Drop &drop = entry.entry;
        drop.position = header.position;
        drop.velocity = header.velocity;
        drop.pickupDelay = 0.0;
        const auto fields = [&](QDataStream &in, quint16) {
            in >> count >> damage >> drop.age >> drop.pickupDelay;
            return true;
        };
        keepHeader(header, drop.kept, false);
        if (!readComponents(drop.kept.components, {known(Component::Item, kindVersion, fields)}) || !std::isfinite(drop.age)
            || !std::isfinite(drop.pickupDelay)) {
            return false;
        }

        const std::optional<QcItemId> item = header.type.isEmpty() ? std::nullopt : m_content.itemId(header.type);
        if (!item) {
            if (!header.type.isEmpty()) {
                warnOnce(u"item"_s, header.type);
            }
            return true;
        }
        addItem(entry, *item, count, damage);
        return true;
    }

    // A stack saved larger than the item allows now (a mod lowered maxStack) is split; only the first
    // part keeps what this version does not use (as QcItemEntityManager::spawn splits).
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
            drop.kept = {};
        }
    }

    bool falling(const Header &header)
    {
        QcEntityRecord::Ordered<QcFallingBlock> entry{{}, header.order};
        QcFallingBlock &block = entry.entry;
        block.motion.position = header.position;
        block.motion.velocity = header.velocity;
        const auto fields = [&block](QDataStream &in, quint16) {
            in >> block.age;
            return true;
        };
        keepHeader(header, block.kept, false);
        if (!readComponents(block.kept.components, {known(Component::FallingBlock, kindVersion, fields)}) || !std::isfinite(block.age)) {
            return false;
        }

        QStringList problems;
        const std::optional<QcBlockId> state = m_content.stateId(header.type, &problems);
        if (!state || *state == qcAirBlockId) {
            warnOnce(u"falling block"_s, header.type);
            return true;
        }
        m_warnings.append(problems);
        block.motion.velocity = clampedVelocity(block.motion.velocity);
        block.age = std::clamp(block.age, 0.0, QcFallingBlockManager::maxSeconds);
        block.block = *state;
        m_record.fallingBlocks.append(entry);
        return true;
    }

    bool stand(const Header &header)
    {
        QcEntityRecord::Ordered<QcArmourStand> entry{{}, header.order};
        QcArmourStand &stand = entry.entry;
        stand.motion.position = header.position;
        stand.yaw = header.yaw;
        const auto fields = [this, &stand](QDataStream &in, quint16) {
            stand.pose = QcProtocolCodec::readStandPose(in);
            in >> stand.name >> stand.item;
            stand.held = standStack(in);
            for (QcItemStack &stack : stand.equipment.stacks) {
                stack = standStack(in);
            }
            in >> stand.cape;
            return true;
        };
        const double y = stand.motion.position.y;
        keepHeader(header, stand.kept, true);
        if (!readComponents(stand.kept.components, {known(Component::ArmourStand, kindVersion, fields)}) || y < QcWorld::voidLimitY
            || y > QcWorld::worldHeight + 64.0) {
            return false;
        }

        stand.yaw = qcNormalizedAngle(stand.yaw);
        stand.name = QcNameText::clean(stand.name, QcArmourStandManager::maxNameLength);
        fitArmour(stand);
        checkCape(stand);
        m_record.stands.append(entry);
        return true;
    }

    bool minecart(const Header &header)
    {
        QcEntityRecord::Ordered<QcMinecart> entry{{}, header.order};
        QcMinecart &cart = entry.entry;
        cart.motion.position = header.position;
        cart.motion.velocity = header.velocity;
        cart.yaw = header.yaw;
        // A container cart's slots (decision E-386) and whether a hopper cart pulls.
        const auto fields = [this, &cart](QDataStream &in, quint16) {
            quint32 slotCount = 0;
            in >> cart.item;
            if (!QcStreamLimits::readCount(in, QcBlockDefinition::maxContainerSlots, slotCount)) {
                return false;
            }
            QcStackReader stacks(m_content, m_path, m_warnings, &m_unknown);
            cart.stacks.resize(slotCount);
            for (QcItemStack &stack : cart.stacks) {
                stack = stacks.read(in);
            }
            in >> cart.enabled;
            return true;
        };
        const double y = cart.motion.position.y;
        keepHeader(header, cart.kept, true);
        if (!readComponents(cart.kept.components, {known(Component::Minecart, kindVersion, fields)}) || y < QcWorld::voidLimitY
            || y > QcWorld::worldHeight + 64.0) {
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

    bool orb(const Header &header)
    {
        QcEntityRecord::Ordered<QcExperienceOrb> entry{{}, header.order};
        QcExperienceOrb &orb = entry.entry;
        qint32 value = 1;
        orb.motion.position = header.position;
        orb.motion.velocity = header.velocity;
        const auto fields = [&](QDataStream &in, quint16) {
            in >> value >> orb.age >> orb.pickupDelay;
            return true;
        };
        keepHeader(header, orb.kept, true);
        if (!readComponents(orb.kept.components, {known(Component::ExperienceOrb, kindVersion, fields)}) || !std::isfinite(orb.age)
            || !std::isfinite(orb.pickupDelay) || value <= 0) {
            return false;
        }

        orb.value = value;
        orb.motion.velocity = clampedVelocity(orb.motion.velocity);
        orb.age = std::clamp(orb.age, 0.0, QcExperienceOrbManager::despawnSeconds);
        orb.pickupDelay = std::clamp(orb.pickupDelay, 0.0, QcExperienceOrbManager::pickupDelay);
        m_record.orbs.append(entry);
        return true;
    }

    bool projectile(const Header &header)
    {
        QcEntityRecord::Ordered<QcProjectile> entry{{}, header.order};
        QcProjectile &shot = entry.entry;
        shot.motion.position = header.position;
        shot.motion.velocity = header.velocity;
        shot.yaw = header.yaw;
        quint8 pickup = 0;
        QString stuckState;
        const auto fields = [&](QDataStream &in, quint16) {
            in >> shot.pitch >> shot.age >> pickup >> shot.damageFactor >> shot.stuck >> shot.stuckIn.x >> shot.stuckIn.y >> shot.stuckIn.z;
            return QcComponentFrames::readText(in, stuckState, maxTypeName);
        };
        keepHeader(header, shot.kept, false);
        if (!readComponents(shot.kept.components, {known(Component::Projectile, kindVersion, fields)}) || !std::isfinite(shot.age)
            || !std::isfinite(shot.pitch) || !std::isfinite(shot.damageFactor) || pickup > quint8(QcProjectilePickup::Never)) {
            return false;
        }

        // A kind a mod took away goes; a block that no longer exists lets it fall.
        if (!m_content.projectile(header.type)) {
            warnOnce(u"projectile"_s, header.type);
            return true;
        }
        shot.type = header.type;
        shot.pickup = QcProjectilePickup(pickup);
        shot.age = std::clamp(shot.age, 0.0, 3600.0);
        shot.damageFactor = std::clamp(shot.damageFactor, 0.0, 100.0);
        shot.motion.velocity = clampedVelocity(shot.motion.velocity);
        const std::optional<QcBlockId> state = shot.stuck ? m_content.stateId(stuckState) : std::nullopt;
        shot.stuck = state.has_value();
        shot.stuckState = state.value_or(0);
        m_record.projectiles.append(entry);
        return true;
    }

    // What a stand wears or holds: one item, by name; unknown items are dropped with a warning.
    QcItemStack standStack(QDataStream &in)
    {
        QString name;
        qint32 count = 0;
        qint32 damage = 0;
        in >> name >> count >> damage;
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

// Writes the entries of one kind: their count, then each one's header and components.
template<typename Entry, typename Write>
void writeEntries(QDataStream &stream, const QList<QcEntityRecord::Ordered<Entry>> &entries, Write write)
{
    stream << quint32(entries.size());
    for (const auto &[entry, order] : entries) {
        write(entry, order);
    }
}

// The known components of each kind, added to the writer of its entry.
void addCreature(QcComponentFrames::Writer &components, const QcMobManager::Stored &mob, const QcContentRegistry &content)
{
    components.add(quint16(Component::Creature), kindVersion, [&mob](QDataStream &out) { out << mob.health << mob.breedCooldown << mob.growUp << mob.bred; });
    if (mob.villager) {
        components.add(quint16(Component::Villager), QcVillagerCodec::version, [&mob](QDataStream &out) { QcVillagerCodec::write(out, *mob.villager); });
    }
    if (mob.carried != qcAirBlockId) {
        const QString state = content.stateName(mob.carried);
        components.add(quint16(Component::Carried), kindVersion, [&state, &mob](QDataStream &out) { out << state << mob.keptForBlock; });
    }
    if (mob.confined) {
        components.add(quint16(Component::Confined), kindVersion, [](QDataStream &) {});
    }
    if (mob.playerBuilt) {
        components.add(quint16(Component::Golem), kindVersion, [&mob](QDataStream &out) { out << mob.playerBuilt; });
    }
}

void addStand(QcComponentFrames::Writer &components, const QcArmourStand &stand, const QcContentRegistry &content)
{
    components.add(quint16(Component::ArmourStand), kindVersion, [&](QDataStream &out) {
        QcProtocolCodec::writeStandPose(out, stand.pose);
        out << stand.name << stand.item;
        QcSaveFile::writeStack(out, stand.held, content);
        for (const QcItemStack &stack : stand.equipment.stacks) {
            QcSaveFile::writeStack(out, stack, content);
        }
        out << stand.cape;
    });
}

void addMinecart(QcComponentFrames::Writer &components, const QcMinecart &cart, const QcContentRegistry &content)
{
    components.add(quint16(Component::Minecart), kindVersion, [&](QDataStream &out) {
        out << cart.item << quint32(cart.stacks.size());
        for (const QcItemStack &stack : cart.stacks) {
            QcSaveFile::writeStack(out, stack, content);
        }
        out << cart.enabled;
    });
}

void writeCreatures(QDataStream &stream, const QcEntityRecord &record, const QcContentRegistry &content)
{
    writeEntries(stream, record.creatures, [&](const QcMobManager::Stored &mob, quint64 order) {
        const Header header{content.mobs()[mob.type].name, mob.position, {}, mob.yaw, order, mob.uid};
        writeEntry(stream, header, mob.kept.components, [&mob, &content](QcComponentFrames::Writer &components) { addCreature(components, mob, content); });
    });
}

void writeItemsAndBlocks(QDataStream &stream, const QcEntityRecord &record, const QcContentRegistry &content)
{
    writeEntries(stream, record.items, [&](const QcItemEntityManager::Drop &drop, quint64 order) {
        const QString name = drop.stack.isEmpty() ? QString() : content.item(drop.stack.item).name;
        const auto fields = [&drop](QDataStream &out) { out << qint32(drop.stack.count) << qint32(drop.stack.damage) << drop.age << drop.pickupDelay; };
        writeEntry(stream, {name, drop.position, drop.velocity, 0.0f, order, drop.kept.uid}, drop.kept.components,
                   [&fields](QcComponentFrames::Writer &components) { components.add(quint16(Component::Item), kindVersion, fields); });
    });
    writeEntries(stream, record.fallingBlocks, [&](const QcFallingBlock &falling, quint64 order) {
        const Header header{content.stateName(falling.block), falling.motion.position, falling.motion.velocity, 0.0f, order, falling.kept.uid};
        const auto fields = [&falling](QDataStream &out) { out << falling.age; };
        writeEntry(stream, header, falling.kept.components,
                   [&fields](QcComponentFrames::Writer &components) { components.add(quint16(Component::FallingBlock), kindVersion, fields); });
    });
}

void writeStands(QDataStream &stream, const QcEntityRecord &record, const QcContentRegistry &content)
{
    writeEntries(stream, record.stands, [&](const QcArmourStand &stand, quint64 order) {
        const Header header{stand.kept.type, stand.motion.position, {}, stand.yaw, order, stand.kept.uid};
        writeEntry(stream, header, stand.kept.components, [&](QcComponentFrames::Writer &components) { addStand(components, stand, content); });
    });
}

void writeCartsAndOrbs(QDataStream &stream, const QcEntityRecord &record, const QcContentRegistry &content)
{
    writeEntries(stream, record.minecarts, [&](const QcMinecart &cart, quint64 order) {
        const Header header{cart.kept.type, cart.motion.position, cart.motion.velocity, cart.yaw, order, cart.kept.uid};
        writeEntry(stream, header, cart.kept.components, [&](QcComponentFrames::Writer &components) { addMinecart(components, cart, content); });
    });
    writeEntries(stream, record.orbs, [&](const QcExperienceOrb &orb, quint64 order) {
        const Header header{orb.kept.type, orb.motion.position, orb.motion.velocity, 0.0f, order, orb.kept.uid};
        const auto fields = [&orb](QDataStream &out) { out << qint32(orb.value) << orb.age << orb.pickupDelay; };
        writeEntry(stream, header, orb.kept.components,
                   [&fields](QcComponentFrames::Writer &components) { components.add(quint16(Component::ExperienceOrb), kindVersion, fields); });
    });
}

void writeProjectiles(QDataStream &stream, const QcEntityRecord &record, const QcContentRegistry &content)
{
    writeEntries(stream, record.projectiles, [&](const QcProjectile &shot, quint64 order) {
        const Header header{shot.type, shot.motion.position, shot.motion.velocity, shot.yaw, order, shot.kept.uid};
        const QString state = shot.stuck ? content.stateName(shot.stuckState) : QString();
        const auto fields = [&shot, &state](QDataStream &out) {
            out << shot.pitch << shot.age << quint8(shot.pickup) << shot.damageFactor << shot.stuck << shot.stuckIn.x << shot.stuckIn.y
                << shot.stuckIn.z << state;
        };
        writeEntry(stream, header, shot.kept.components,
                   [&fields](QcComponentFrames::Writer &components) { components.add(quint16(Component::Projectile), kindVersion, fields); });
    });
}

} // namespace

namespace QcEntityCodec {

QByteArray encode(const QcEntityRecord &record, const QcContentRegistry &content)
{
    QByteArray blob;
    QDataStream stream(&blob, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    writeCreatures(stream, record, content);
    writeItemsAndBlocks(stream, record, content);
    writeStands(stream, record, content);
    writeCartsAndOrbs(stream, record, content);
    writeProjectiles(stream, record, content);
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
