#include "server/storage/QcEntityRecords.h"

#include "server/entities/QcEntities.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcEntityRecords {

namespace {

// The record of `chunk`, made on first use.
QcEntityRecord &recordOf(QHash<QcChunkPos, QcEntityRecord> &records, QcChunkPos chunk)
{
    QcEntityRecord &record = records[chunk];
    record.chunk = chunk;
    return record;
}

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

// Adds the entities `wanted` accepts to their records; `wanted(chunk)`.
template<typename Wanted>
void collectInto(const QcEntities &entities, QHash<QcChunkPos, QcEntityRecord> &records, Wanted wanted)
{
    for (const QcItemEntity &item : entities.items().items()) {
        if (const QcChunkPos chunk = QcEntities::chunkOf(item); wanted(chunk)) {
            recordOf(records, chunk).items.append(
                {{item.stack, item.motion.position, item.motion.velocity, item.pickupDelay, item.age, item.kept}, item.id});
        }
    }
    // Waiting drops follow the items; on the next start they wait again if there is no room.
    quint64 waiting = QcEntityRecord::waitingOrderBase;
    for (const QcItemEntityManager::Drop &drop : entities.items().queued()) {
        if (const QcChunkPos chunk = chunkAt(drop.position); wanted(chunk)) {
            recordOf(records, chunk).items.append({drop, waiting});
        }
        ++waiting;
    }
    for (const QcFallingBlock &falling : entities.fallingBlocks().all()) {
        if (const QcChunkPos chunk = QcEntities::chunkOf(falling); wanted(chunk)) {
            recordOf(records, chunk).fallingBlocks.append({falling, falling.id});
        }
    }
    for (const QcCreature &creature : entities.creatures().mobs()) {
        if (const QcChunkPos chunk = QcEntities::chunkOf(creature); creature.persistent && wanted(chunk)) {
            recordOf(records, chunk).creatures.append({QcMobManager::storedOf(creature), creature.id});
        }
    }
    for (const QcArmourStand &stand : entities.stands().stands()) {
        if (const QcChunkPos chunk = QcEntities::chunkOf(stand); wanted(chunk)) {
            recordOf(records, chunk).stands.append({stand, stand.id});
        }
    }
    for (const QcMinecart &cart : entities.minecarts().all()) {
        if (const QcChunkPos chunk = QcEntities::chunkOf(cart); wanted(chunk)) {
            recordOf(records, chunk).minecarts.append({cart, cart.id});
        }
    }
    for (const QcExperienceOrb &orb : entities.orbs().orbs()) {
        if (const QcChunkPos chunk = QcEntities::chunkOf(orb); wanted(chunk)) {
            recordOf(records, chunk).orbs.append({orb, orb.id});
        }
    }
}

} // namespace

QHash<QcChunkPos, QcEntityRecord> collect(const QcEntities &entities)
{
    QHash<QcChunkPos, QcEntityRecord> records;
    collectInto(entities, records, [](QcChunkPos) { return true; });
    return records;
}

QHash<QcChunkPos, QcEntityRecord> take(QcEntities &entities, const QSet<QcChunkPos> &chunks)
{
    QHash<QcChunkPos, QcEntityRecord> records;
    const auto in = [&chunks](QcChunkPos chunk) { return chunks.contains(chunk); };
    collectInto(std::as_const(entities), records, in);

    const auto gone = [&in](const QcEntity &entity) { return in(QcEntities::chunkOf(entity)); };
    entities.items().editableItems().removeIf(gone);
    entities.items().takeQueued([&in](const QcItemEntityManager::Drop &drop) { return in(chunkAt(drop.position)); });
    entities.fallingBlocks().editableAll().removeIf(gone);
    entities.creatures().editableMobs().removeIf(gone);
    entities.stands().editableStands().removeIf(gone);
    entities.minecarts().editableAll().removeIf(gone);
    entities.orbs().editableOrbs().removeIf(gone);
    entities.movingBlocks().editableAll().removeIf(gone);
    return records;
}

QStringList apply(const QcEntityRecord &record, QcEntities &entities)
{
    const auto newId = [&entities] { return entities.newId(); };
    entities.items().adopt(inOrder(record.items), newId);
    QStringList warnings;
    if (const qsizetype left = entities.fallingBlocks().adopt(inOrder(record.fallingBlocks), newId); left > 0) {
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
    if (const qsizetype merged = entities.orbs().adopt(inOrder(record.orbs), newId); merged > 0) {
        warnings.append(u"%1 experience orbs beyond the limit of %2 joined the nearest orb"_s.arg(merged).arg(entities.orbs().limit()));
    }
    return warnings;
}

} // namespace QcEntityRecords
