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
#include "server/storage/QcCreatureComponents.h"
#include "server/storage/QcHangingCodec.h"
#include "server/storage/QcItemDataCodec.h"
#include "server/storage/QcSaveFile.h"
#include "server/storage/QcStackReader.h"

#include <QIODevice>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

using Component = QcEntityCodec::Component;
using QcEntityCodec::kindVersion;
using QcEntityCodec::maxTypeName;

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

[[nodiscard]] QcVec3 readVec(QDataStream &stream)
{
    QcVec3 v;
    stream >> v.x >> v.y >> v.z;
    return v;
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

// Reads the entries of one chunk record from a region file and checks them: every entity lies in
// the record's chunk, at a finite place inside the world.
class RecordReader
{
public:
    // Each unknown name is reported once to `warnings` (once per world, not once per chunk).
    RecordReader(QDataStream &stream, const QcContentRegistry &content, const QString &path, QcLoadWarnings &warnings, QcEntityRecord &record)
        : m_stream(stream)
        , m_content(content)
        , m_path(path)
        , m_warnings(warnings)
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
        const std::optional<quint32> countRead = QcStreamLimits::readCount(m_stream, QcEntityCodec::maxPerChunk);
        if (!countRead) {
            return false;
        }
        const quint32 count = *countRead;
        for (quint32 i = 0; i < count; ++i) {
            const std::optional<Header> header = readHeader();
            if (!header || !read(*header)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::optional<Header> readHeader()
    {
        std::optional<QString> type = QcComponentFrames::readText(m_stream, maxTypeName);
        if (!type) {
            return std::nullopt;
        }
        Header header;
        header.type = std::move(*type);
        header.position = readVec(m_stream);
        header.velocity = readVec(m_stream);
        m_stream >> header.yaw >> header.order >> header.uid;
        if (m_stream.status() != QDataStream::Ok || !inChunk(header.position) || !isFinite(header.velocity) || !std::isfinite(header.yaw)) {
            return std::nullopt;
        }
        return header;
    }

    // A finite place in this record's chunk, with room above and below the world.
    bool inChunk(const QcVec3 &feet) const
    {
        return isFinite(feet) && std::abs(feet.x) <= QcWorld::maxHorizontalBlock && std::abs(feet.z) <= QcWorld::maxHorizontalBlock
               && std::abs(feet.y) <= 4.0 * QcWorld::worldHeight && QcPhysics::blockContaining(feet).chunk() == m_record.chunk;
    }

    // Reads the known components of an entry and returns the others to keep, those beyond
    // QcComponentFrames::maxKept dropped with a warning; none for damaged data.
    [[nodiscard]] std::optional<QcEntityComponents> readComponents(std::initializer_list<QcComponentFrames::Known> readers)
    {
        std::optional<QcComponentFrames::ReadComponents> components = QcComponentFrames::read(m_stream, readers);
        if (!components) {
            return std::nullopt;
        }
        return kept(std::move(*components));
    }

    // The unknown components of an entry to keep; those beyond QcComponentFrames::maxKept are
    // dropped with a warning.
    [[nodiscard]] QcEntityComponents kept(QcComponentFrames::ReadComponents components)
    {
        if (components.dropped > 0) {
            const QString text = u"%1: an entity with more than %2 unknown components; %3 of them dropped"_s;
            m_warnings.add(text.arg(m_path).arg(QcComponentFrames::maxKept).arg(components.dropped));
        }
        return std::move(components.kept);
    }

    // The header fields that the entries of a kind do not use in this version (decision E-420).
    [[nodiscard]] static QcEntityKept keptOf(const Header &header, bool withType)
    {
        return {{}, header.uid, withType ? header.type : QString()};
    }

    void warnOnce(const QString &kind, const QString &name)
    {
        m_warnings.addOnce(kind + u':' + name, u"%1: unknown %2 '%3' dropped (in this world reported only here)"_s.arg(m_path, kind, name));
    }

    bool creature(const Header &header)
    {
        QcMobManager::Stored stored;
        stored.position = header.position;
        stored.yaw = std::fmod(header.yaw, 360.0f);
        stored.uid = header.uid;
        const std::optional<int> type = m_content.mobIndex(header.type);
        // Without its creature component it is a grown creature at full health.
        stored.health = type ? m_content.mobs()[*type].maxHealth : 1.0;
        std::optional<QcCreatureComponents::Read> read = QcCreatureComponents::read(m_stream, std::move(stored));
        if (!read) {
            return false;
        }

        QcEntityRecord::Ordered<QcMobManager::Stored> entry{std::move(read->mob), header.order};
        QcMobManager::Stored &mob = entry.entry;
        mob.kept.components = kept(std::move(read->components));
        if (!read->carried.isEmpty()) {
            mob.carried = resolvedCarried(read->carried);
        }
        if (!plausibleCreature(mob)) {
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

    // The carried block of a read creature; one the content no longer has is lost with a warning.
    [[nodiscard]] QcBlockId resolvedCarried(const QString &carried)
    {
        const std::optional<QcBlockId> state = m_content.stateId(carried);
        if (!state) {
            warnOnce(u"block"_s, carried);
        }
        return state.value_or(qcAirBlockId);
    }

    // Whether the numbers of a read creature can be.
    static bool plausibleCreature(const QcMobManager::Stored &mob)
    {
        const bool finite = std::isfinite(mob.health) && std::isfinite(mob.breedCooldown) && std::isfinite(mob.growUp) && std::isfinite(mob.layTimer);
        return finite && mob.uid != 0 && mob.breedCooldown <= maxBreedingSeconds && mob.growUp <= maxBreedingSeconds
               && mob.layTimer <= maxBreedingSeconds;
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
        std::optional<QcItemData> data = QcItemData{};
        const auto dataFields = [&](QDataStream &in, quint16) {
            data = QcStackReader(m_content, m_path, m_warnings).readData(in, QcItemData::maxDepth);
            return data.has_value();
        };
        drop.kept = keptOf(header, false);
        std::optional<QcEntityComponents> components =
            readComponents({known(Component::Item, kindVersion, fields), known(Component::ItemData, kindVersion, dataFields)});
        if (!components || !std::isfinite(drop.age) || !std::isfinite(drop.pickupDelay)) {
            return false;
        }
        drop.kept.components = std::move(*components);

        const std::optional<QcItemId> item = header.type.isEmpty() ? std::nullopt : m_content.itemId(header.type);
        if (!item) {
            if (!header.type.isEmpty()) {
                warnOnce(u"item"_s, header.type);
            }
            return true;
        }
        addItem(entry, QcStackReader::Saved{*item, count, damage, std::move(*data)});
        return true;
    }

    // A stack saved larger than the item allows now (a mod lowered maxStack) is split; only the first
    // part keeps what this version does not use, of the entry and of the stack's data
    // (QcItemStack::split, as in QcItemEntityManager::spawn): an unknown component may hold items,
    // which a copy would multiply. A stack with contents is one item, whatever count was saved, for the same reason.
    void addItem(const QcEntityRecord::Ordered<QcItemEntityManager::Drop> &read, const QcStackReader::Saved &saved)
    {
        QcEntityRecord::Ordered<QcItemEntityManager::Drop> entry = read;
        QcItemEntityManager::Drop &drop = entry.entry;
        drop.velocity = clampedVelocity(drop.velocity);
        drop.age = std::clamp(drop.age, 0.0, QcItemEntityManager::despawnSeconds);
        drop.pickupDelay = std::clamp(drop.pickupDelay, 0.0, QcItemEntityManager::thrownPickupDelay);
        const QcItemStack fitted = QcStackReader(m_content, m_path, m_warnings).fitted(saved);
        const int count = std::clamp(saved.count, 1, QcItemDefinition::maxStackLimit);
        const QList<QcItemStack> parts = fitted.withCount(count).split(m_content.item(fitted.item).maxStack);
        for (const QcItemStack &part : parts) {
            drop.stack = part;
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
        std::optional<qint32> fuse;
        const auto fuseField = [&fuse](QDataStream &in, quint16) {
            fuse = 0;
            in >> *fuse;
            return true;
        };
        block.kept = keptOf(header, false);
        std::optional<QcEntityComponents> components =
            readComponents({known(Component::FallingBlock, kindVersion, fields), known(Component::Fuse, kindVersion, fuseField)});
        if (!components || !std::isfinite(block.age)) {
            return false;
        }
        block.kept.components = std::move(*components);

        const QcContentRegistry::ParsedState parsed = m_content.parseState(header.type);
        const std::optional<QcBlockId> state = parsed.id;
        const QStringList &problems = parsed.problems;
        if (!state || *state == qcAirBlockId) {
            warnOnce(u"falling block"_s, header.type);
            return true;
        }
        m_warnings.add(problems);
        block.motion.velocity = clampedVelocity(block.motion.velocity);
        block.age = std::clamp(block.age, 0.0, QcFallingBlockManager::maxSeconds);
        block.block = *state;
        if (fuse) {
            primed(entry, *fuse);
            return true;
        }
        m_record.fallingBlocks.append(entry);
        return true;
    }

    // A falling block entry with a fuse: a lit explosive (decision E-601).
    void primed(const QcEntityRecord::Ordered<QcFallingBlock> &entry, int fuse)
    {
        QcEntityRecord::Ordered<QcPrimedExplosive> primed{{}, entry.order};
        primed.entry.motion = entry.entry.motion;
        primed.entry.block = entry.entry.block;
        primed.entry.fuse = std::clamp(fuse, 0, QcBlastDefinition::maxFuseTicks);
        primed.entry.kept = entry.entry.kept;
        m_record.primedExplosives.append(primed);
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
        stand.kept = keptOf(header, true);
        std::optional<QcEntityComponents> components = readComponents({known(Component::ArmourStand, kindVersion, fields)});
        if (!components || y < QcWorld::voidLimitY || y > QcWorld::worldHeight + 64.0) {
            return false;
        }
        stand.kept.components = std::move(*components);

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
            in >> cart.item;
            const std::optional<quint32> slotCountRead = QcStreamLimits::readCount(in, QcBlockDefinition::maxContainerSlots);
            if (!slotCountRead) {
                return false;
            }
            const quint32 slotCount = *slotCountRead;
            QcStackReader stacks(m_content, m_path, m_warnings);
            cart.stacks.resize(slotCount);
            for (QcItemStack &stack : cart.stacks) {
                stack = stacks.read(in);
            }
            in >> cart.enabled;
            return true;
        };
        const double y = cart.motion.position.y;
        cart.kept = keptOf(header, true);
        std::optional<QcEntityComponents> components = readComponents({known(Component::Minecart, kindVersion, fields)});
        if (!components || y < QcWorld::voidLimitY || y > QcWorld::worldHeight + 64.0) {
            return false;
        }
        cart.kept.components = std::move(*components);

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
        orb.kept = keptOf(header, true);
        std::optional<QcEntityComponents> components = readComponents({known(Component::ExperienceOrb, kindVersion, fields)});
        if (!components || !std::isfinite(orb.age) || !std::isfinite(orb.pickupDelay) || value <= 0) {
            return false;
        }
        orb.kept.components = std::move(*components);

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
            const std::optional<QString> read = QcComponentFrames::readText(in, maxTypeName);
            stuckState = read.value_or(QString());
            return read.has_value();
        };
        shot.kept = keptOf(header, false);
        std::optional<QcEntityComponents> components = readComponents({known(Component::Projectile, kindVersion, fields)});
        if (!components || !std::isfinite(shot.age) || !std::isfinite(shot.pitch) || !std::isfinite(shot.damageFactor)
            || pickup > quint8(QcProjectilePickup::Never)) {
            return false;
        }
        shot.kept.components = std::move(*components);

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
        const QcItemStack stack = QcStackReader(m_content, m_path, m_warnings).read(in);
        return stack.isEmpty() ? QcItemStack{} : stack.withCount(1);
    }

    void fitArmour(QcArmourStand &stand)
    {
        for (std::size_t slot = 0; slot < QcEquipment::slotCount; ++slot) {
            QcItemStack &stack = stand.equipment.stacks[slot];
            if (!stack.isEmpty() && !QcArmour::fits(m_content, stack, QcEquipmentSlot(slot))) {
                m_warnings.add(u"%1: '%2' is no armour for its slot on a stand; dropped"_s.arg(m_path, m_content.item(stack.item).name));
                stack = {};
            }
        }
    }

    void checkCape(QcArmourStand &stand)
    {
        const QcAppearanceDefinition *cape = stand.cape.isEmpty() ? nullptr : m_content.assets().appearance(stand.cape);
        if (!stand.cape.isEmpty() && (!cape || cape->kind != QcAppearanceDefinition::Kind::Cape)) {
            warnOnce(u"cape on a stand"_s, stand.cape);
            stand.cape.clear();
        }
    }

    QDataStream &m_stream;
    const QcContentRegistry &m_content;
    const QString &m_path;
    QcLoadWarnings &m_warnings;
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
        writeEntry(stream, header, mob.kept.components, [&mob, &content](QcComponentFrames::Writer &components) -> QcComponentFrames::Writer & { return QcCreatureComponents::write(components, mob, content); });
    });
}

void writeItemsAndBlocks(QDataStream &stream, const QcEntityRecord &record, const QcContentRegistry &content)
{
    writeEntries(stream, record.items, [&](const QcItemEntityManager::Drop &drop, quint64 order) {
        const QString name = drop.stack.isEmpty() ? QString() : content.item(drop.stack.item).name;
        const auto fields = [&drop](QDataStream &out) { out << qint32(drop.stack.count) << qint32(drop.stack.damage) << drop.age << drop.pickupDelay; };
        const auto dataFields = [&drop, &content](QDataStream &out) { QcItemDataCodec::write(out, drop.stack.itemData(), content); };
        writeEntry(stream, {name, drop.position, drop.velocity, 0.0f, order, drop.kept.uid}, drop.kept.components,
                   [&](QcComponentFrames::Writer &components) {
                       components.add(quint16(Component::Item), kindVersion, fields);
                       if (drop.stack.data) {
                           components.add(quint16(Component::ItemData), kindVersion, dataFields);
                       }
                   });
    });
    // Lit explosives are falling block entries with a fuse (decision E-601), in the same list.
    stream << quint32(record.fallingBlocks.size() + record.primedExplosives.size());
    for (const auto &[falling, order] : record.fallingBlocks) {
        const Header header{content.stateName(falling.block), falling.motion.position, falling.motion.velocity, 0.0f, order, falling.kept.uid};
        const auto fields = [&falling](QDataStream &out) { out << falling.age; };
        writeEntry(stream, header, falling.kept.components,
                   [&fields](QcComponentFrames::Writer &components) { components.add(quint16(Component::FallingBlock), kindVersion, fields); });
    }
    for (const auto &[primed, order] : record.primedExplosives) {
        const Header header{content.stateName(primed.block), primed.motion.position, primed.motion.velocity, 0.0f, order, primed.kept.uid};
        writeEntry(stream, header, primed.kept.components, [&primed](QcComponentFrames::Writer &components) {
            components.add(quint16(Component::FallingBlock), kindVersion, [](QDataStream &out) { out << 0.0; });
            components.add(quint16(Component::Fuse), kindVersion, [&primed](QDataStream &out) { out << qint32(primed.fuse); });
        });
    }
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
    QcHangingCodec::write(stream, record.hangings, content);
    return blob;
}

[[nodiscard]] QcResult<QcEntityRecord> decode(const QByteArray &blob, QcChunkPos chunk, const QcContentRegistry &content,
                                              QcLoadWarnings &warnings)
{
    QDataStream stream(blob);
    stream.setVersion(QcSaveFile::streamVersion);
    QcEntityRecord record;
    record.chunk = chunk;
    const QString where = u"chunk %1, %2"_s.arg(chunk.x).arg(chunk.z);
    quint8 populated = 0;
    const bool read = RecordReader(stream, content, where, warnings, record).read();
    stream >> populated;
    std::optional<QList<QcEntityRecord::Ordered<QcHangingEntity>>> hangings =
        read && stream.status() == QDataStream::Ok ? QcHangingCodec::read(stream, chunk, content, warnings) : std::nullopt;
    if (!hangings || stream.status() != QDataStream::Ok || !stream.atEnd() || populated > 1) {
        return qcError(u"damaged entities of chunk %1, %2"_s.arg(chunk.x).arg(chunk.z));
    }
    record.hangings = std::move(*hangings);
    record.populated = populated == 1;
    return record;
}

} // namespace QcEntityCodec
