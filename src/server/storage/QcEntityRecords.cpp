#include "QcEntityRecords.h"

#include "QcEntities.h"

#include <QHash>

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

// The entries of one kind from every record, in their saved order.
template<typename Entry>
QList<Entry> inOrder(const QList<QcEntityRecord> &records, QList<QcEntityRecord::Ordered<Entry>> QcEntityRecord::*kind)
{
    QList<QcEntityRecord::Ordered<Entry>> all;
    for (const QcEntityRecord &record : records) {
        all.append(record.*kind);
    }
    std::ranges::stable_sort(all, {}, &QcEntityRecord::Ordered<Entry>::order);

    QList<Entry> entries;
    entries.reserve(all.size());
    for (const QcEntityRecord::Ordered<Entry> &ordered : std::as_const(all)) {
        entries.append(ordered.entry);
    }
    return entries;
}

void collectItems(const QcItemEntityManager &items, QHash<QcChunkPos, QcEntityRecord> &records)
{
    for (const QcItemEntity &item : items.items()) {
        recordOf(records, QcEntities::chunkOf(item)).items.append({{item.stack, item.motion.position, item.motion.velocity, item.pickupDelay, item.age}, item.id});
    }
    // Waiting drops follow the items; on the next start they wait again if there is no room.
    quint64 waiting = QcEntityRecord::waitingOrderBase;
    for (const QcItemEntityManager::Drop &drop : items.queued()) {
        recordOf(records, chunkAt(drop.position)).items.append({drop, waiting++});
    }
}

} // namespace

QcWorldEntityRecords collect(const QcEntities &entities)
{
    QHash<QcChunkPos, QcEntityRecord> records;
    collectItems(entities.items(), records);

    for (const QcFallingBlock &falling : entities.fallingBlocks().all()) {
        recordOf(records, QcEntities::chunkOf(falling)).fallingBlocks.append({falling, falling.id});
    }
    for (const QcCreature &creature : entities.creatures().mobs()) {
        if (creature.persistent) {
            recordOf(records, QcEntities::chunkOf(creature)).creatures.append({QcMobManager::storedOf(creature), creature.id});
        }
    }
    for (const QcArmourStand &stand : entities.stands().stands()) {
        recordOf(records, QcEntities::chunkOf(stand)).stands.append({stand, stand.id});
    }

    QcWorldEntityRecords saved{records.values(), entities.creatures().populatedChunks()};
    std::ranges::sort(saved.records, [](const QcEntityRecord &a, const QcEntityRecord &b) {
        return std::pair{a.chunk.x, a.chunk.z} < std::pair{b.chunk.x, b.chunk.z};
    });
    return saved;
}

QStringList apply(const QcWorldEntityRecords &saved, QcEntities &entities)
{
    const auto newId = [&entities] { return entities.newId(); };
    entities.items().restore(inOrder(saved.records, &QcEntityRecord::items), newId);
    entities.fallingBlocks().restore(inOrder(saved.records, &QcEntityRecord::fallingBlocks), newId);
    entities.creatures().restore(inOrder(saved.records, &QcEntityRecord::creatures), saved.populated, newId);

    QList<QcArmourStand> stands = inOrder(saved.records, &QcEntityRecord::stands);
    for (QcArmourStand &stand : stands) {
        stand.id = entities.newId();
    }

    QStringList warnings;
    if (const qsizetype dropped = entities.stands().restore(std::move(stands)); dropped > 0) {
        warnings.append(u"%1 armour stands beyond the limit of %2 were left out"_s.arg(dropped).arg(entities.stands().limit()));
    }
    return warnings;
}

} // namespace QcEntityRecords
