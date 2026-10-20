#include "server/storage/QcEntityRecords.h"

#include "server/entities/QcEntities.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcEntityRecords {

namespace {

QcChunkPos chunkAt(const QcVec3 &feet)
{
    return QcPhysics::blockContaining(feet).chunk();
}

// The entries of one kind of a record in their saved order.
template<typename Entry>
QList<Entry> inOrder(QList<QcEntityRecord::Ordered<Entry>> entries)
{
    std::ranges::stable_sort(entries, {}, &QcEntityRecord::Ordered<Entry>::order);
    QList<Entry> sorted;
    sorted.reserve(entries.size());
    for (const QcEntityRecord::Ordered<Entry> &ordered : std::as_const(entries)) {
        sorted.append(ordered.entry);
    }
    return sorted;
}

// The records of the chunks while entities are collected into them.
class RecordCollector
{
public:
    // Adds the entities `wanted` accepts to their records; `wanted(chunk)`.
    template<typename Wanted>
    void add(const QcEntities &entities, Wanted wanted);

    QHash<QcChunkPos, QcEntityRecord> take() { return std::move(m_records); }

private:
    // Adds the stands, minecarts, experience orbs, projectiles, frames and paintings `wanted` accepts to their records;
    // transient projectiles (fishing bobbers, decision E-503) are never saved.
    template<typename Wanted>
    void addObjects(const QcEntities &entities, Wanted wanted);

    // The record of `chunk`, made on first use.
    QcEntityRecord &recordOf(QcChunkPos chunk)
    {
        QcEntityRecord &record = m_records[chunk];
        record.chunk = chunk;
        return record;
    }

    QHash<QcChunkPos, QcEntityRecord> m_records;
};

template<typename Wanted>
void RecordCollector::addObjects(const QcEntities &entities, Wanted wanted)
{
    for (const QcArmourStand &stand : entities.stands().stands()) {
        if (const QcChunkPos chunk = QcEntities::chunkOf(stand); wanted(chunk)) {
            recordOf(chunk).stands.append({stand, stand.id});
        }
    }
    for (const QcMinecart &cart : entities.minecarts().all()) {
        if (const QcChunkPos chunk = QcEntities::chunkOf(cart); wanted(chunk)) {
            recordOf(chunk).minecarts.append({cart, cart.id});
        }
    }
    for (const QcExperienceOrb &orb : entities.orbs().orbs()) {
        if (const QcChunkPos chunk = QcEntities::chunkOf(orb); wanted(chunk)) {
            recordOf(chunk).orbs.append({orb, orb.id});
        }
    }
    for (const QcProjectile &shot : entities.projectiles().all()) {
        if (const QcChunkPos chunk = QcEntities::chunkOf(shot); !shot.transient && wanted(chunk)) {
            recordOf(chunk).projectiles.append({shot, shot.id});
        }
    }
    for (const QcHangingEntity &hanging : entities.hangings().all()) {
        if (const QcChunkPos chunk = QcEntities::chunkOf(hanging); wanted(chunk)) {
            recordOf(chunk).hangings.append({hanging, hanging.id});
        }
    }
}

template<typename Wanted>
void RecordCollector::add(const QcEntities &entities, Wanted wanted)
{
    for (const QcItemEntity &item : entities.items().items()) {
        if (const QcChunkPos chunk = QcEntities::chunkOf(item); wanted(chunk)) {
            recordOf(chunk).items.append(
                {{item.stack, item.motion.position, item.motion.velocity, item.pickupDelay, item.age, item.kept}, item.id});
        }
    }
    // Waiting drops follow the items; on the next start they wait again if there is no room.
    quint64 waiting = QcEntityRecord::waitingOrderBase;
    for (const QcItemEntityManager::Drop &drop : entities.items().queued()) {
        if (const QcChunkPos chunk = chunkAt(drop.position); wanted(chunk)) {
            recordOf(chunk).items.append({drop, waiting});
        }
        ++waiting;
    }
    for (const QcFallingBlock &falling : entities.fallingBlocks().all()) {
        if (const QcChunkPos chunk = QcEntities::chunkOf(falling); wanted(chunk)) {
            recordOf(chunk).fallingBlocks.append({falling, falling.id});
        }
    }
    for (const QcPrimedExplosive &primed : entities.explosives().all()) {
        if (const QcChunkPos chunk = QcEntities::chunkOf(primed); wanted(chunk)) {
            recordOf(chunk).primedExplosives.append({primed, primed.id});
        }
    }
    for (const QcCreature &creature : entities.creatures().mobs()) {
        if (const QcChunkPos chunk = QcEntities::chunkOf(creature); creature.persistent && wanted(chunk)) {
            recordOf(chunk).creatures.append({QcMobManager::storedOf(creature), creature.id});
        }
    }
    addObjects(entities, wanted);
}

} // namespace

QHash<QcChunkPos, QcEntityRecord> collect(const QcEntities &entities)
{
    RecordCollector collector;
    collector.add(entities, [](QcChunkPos) { return true; });
    return collector.take();
}

QHash<QcChunkPos, QcEntityRecord> take(QcEntities &entities, const QSet<QcChunkPos> &chunks)
{
    const auto in = [&chunks](QcChunkPos chunk) { return chunks.contains(chunk); };
    RecordCollector collector;
    collector.add(std::as_const(entities), in);
    QHash<QcChunkPos, QcEntityRecord> records = collector.take();

    const auto gone = [&in](const QcEntity &entity) { return in(QcEntities::chunkOf(entity)); };
    entities.items().editableItems().removeIf(gone);
    entities.items().takeQueued([&in](const QcItemEntityManager::Drop &drop) { return in(chunkAt(drop.position)); });
    entities.fallingBlocks().editableAll().removeIf(gone);
    entities.explosives().editableAll().removeIf(gone);
    entities.creatures().unload(gone);
    entities.stands().editableStands().removeIf(gone);
    entities.hangings().removeIf(gone);
    entities.minecarts().editableAll().removeIf(gone);
    entities.orbs().editableOrbs().removeIf(gone);
    entities.projectiles().editableAll().removeIf(gone);
    entities.movingBlocks().editableAll().removeIf(gone);
    return records;
}

QStringList apply(const QcEntityRecord &record, QcEntities &entities)
{
    const auto newId = [&entities] { return entities.newId(); };
    entities.items().adopt(inOrder(record.items), newId);
    QStringList warnings;
    // Lit explosives beyond their limit fall on as unlit blocks and land as explosives.
    QList<QcFallingBlock> falling = inOrder(record.fallingBlocks);
    if (const QList<QcPrimedExplosive> left = entities.explosives().adopt(inOrder(record.primedExplosives), newId); !left.isEmpty()) {
        warnings.append(u"%1 lit explosives beyond the limit fall as unlit blocks"_s.arg(left.size()));
        for (const QcPrimedExplosive &primed : left) {
            falling.append(QcPrimedExplosives::unlit(primed));
        }
    }
    if (const qsizetype left = entities.fallingBlocks().adopt(falling, newId); left > 0) {
        warnings.append(u"%1 falling blocks beyond the limit were left out"_s.arg(left));
    }
    entities.creatures().restore(inOrder(record.creatures), {}, newId);

    QList<QcArmourStand> stands = inOrder(record.stands);
    for (QcArmourStand &stand : stands) {
        stand.id = entities.newId();
    }
    if (const qsizetype dropped = entities.stands().adopt(stands); dropped > 0) {
        warnings.append(u"%1 armour stands beyond the limit of %2 were left out"_s.arg(dropped).arg(entities.stands().limit()));
    }

    QList<QcMinecart> carts = inOrder(record.minecarts);
    for (QcMinecart &cart : carts) {
        cart.id = entities.newId();
    }
    if (const qsizetype dropped = entities.minecarts().adopt(carts); dropped > 0) {
        warnings.append(u"%1 minecarts beyond the limit of %2 were left out"_s.arg(dropped).arg(entities.minecarts().limit()));
    }
    QList<QcHangingEntity> hangings = inOrder(record.hangings);
    for (QcHangingEntity &hanging : hangings) {
        hanging.id = entities.newId();
    }
    if (const qsizetype dropped = entities.hangings().adopt(hangings); dropped > 0) {
        warnings.append(u"%1 item frames and paintings beyond the limit of %2 were left out"_s.arg(dropped).arg(entities.hangings().limit()));
    }
    if (const qsizetype merged = entities.orbs().adopt(inOrder(record.orbs), newId); merged > 0) {
        warnings.append(u"%1 experience orbs beyond the limit of %2 joined the nearest orb"_s.arg(merged).arg(entities.orbs().limit()));
    }
    if (const qsizetype left = entities.projectiles().adopt(inOrder(record.projectiles), newId); left > 0) {
        warnings.append(u"%1 projectiles beyond the limits were left out"_s.arg(left));
    }
    return warnings;
}

} // namespace QcEntityRecords
