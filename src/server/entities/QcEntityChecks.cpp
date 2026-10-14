#include "QcEntityChecks.h"

#include "QcEntities.h"
#include "QcEntityRecords.h"

#include <QHash>

using namespace Qt::StringLiterals;

namespace QcEntityChecks {

namespace {

// Ids given twice, and entities whose kind does not name the table they lie in.
void checkIdsAndTables(const QcEntities &entities, QStringList &problems)
{
    QHash<quint32, QcEntityKind> seen;
    const auto note = [&](const QcEntity &entity, QcEntityKind table) {
        if (entity.kind != table) {
            problems.append(u"entity %1 of kind %2 lies in the table of %3"_s.arg(entity.id)
                                .arg(QcEntityKinds::name(entity.kind), QcEntityKinds::name(table)));
        }
        if (const auto it = seen.constFind(entity.id); it != seen.constEnd()) {
            problems.append(u"id %1 is given twice (%2 and %3)"_s.arg(entity.id).arg(QcEntityKinds::name(*it), QcEntityKinds::name(entity.kind)));
        }
        seen.insert(entity.id, entity.kind);
    };
    for (const QcCreature &creature : entities.creatures().mobs()) {
        note(creature, QcEntityKind::Creature);
    }
    for (const QcItemEntity &item : entities.items().items()) {
        note(item, QcEntityKind::Item);
    }
    for (const QcFallingBlock &falling : entities.fallingBlocks().all()) {
        note(falling, QcEntityKind::FallingBlock);
    }
    for (const QcArmourStand &stand : entities.stands().stands()) {
        note(stand, QcEntityKind::ArmourStand);
    }
    for (const QcMovingBlock &moving : entities.movingBlocks().all()) {
        note(moving, QcEntityKind::MovingBlock);
    }
    // Every client that became a player, also one that is dead or leaving.
    for (const std::unique_ptr<QcServerClient> &client : entities.clients()) {
        if (client->id != 0) {
            note(*client, QcEntityKind::Player);
        }
    }
}

template<typename Entry, typename Position>
void checkChunks(const QList<QcEntityRecord::Ordered<Entry>> &entries, QcChunkPos chunk, const char *kind, Position position,
                 QStringList &problems)
{
    for (const QcEntityRecord::Ordered<Entry> &ordered : entries) {
        if (QcPhysics::blockContaining(position(ordered.entry)).chunk() != chunk) {
            problems.append(u"a saved %1 (order %2) is in the record of chunk %3, %4 but not in that chunk"_s.arg(QLatin1StringView(kind))
                                .arg(ordered.order)
                                .arg(chunk.x)
                                .arg(chunk.z));
        }
    }
}

} // namespace

QStringList problems(const QcEntities &entities)
{
    QStringList problems;
    checkIdsAndTables(entities, problems);

    const QcWorldEntityRecords saved = QcEntityRecords::collect(entities);
    for (const QcEntityRecord &record : saved.records) {
        checkChunks(record.creatures, record.chunk, "creature", [](const QcMobManager::Stored &stored) { return stored.position; }, problems);
        checkChunks(record.items, record.chunk, "item", [](const QcItemEntityManager::Drop &drop) { return drop.position; }, problems);
        checkChunks(record.fallingBlocks, record.chunk, "falling block", [](const QcFallingBlock &falling) { return falling.motion.position; },
                    problems);
        checkChunks(record.stands, record.chunk, "armour stand", [](const QcArmourStand &stand) { return stand.motion.position; }, problems);
    }
    return problems;
}

} // namespace QcEntityChecks
