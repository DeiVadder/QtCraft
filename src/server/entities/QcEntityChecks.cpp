#include "server/entities/QcEntityChecks.h"

#include "server/entities/QcEntities.h"
#include "server/storage/QcEntityRecords.h"

#include <QHash>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcEntityChecks {

namespace {

// Ids given twice, and entities whose kind does not name the table they lie in.
[[nodiscard]] QStringList checkIdsAndTables(const QcEntities &entities)
{
    QStringList problems;
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
    for (const QcPrimedExplosive &primed : entities.explosives().all()) {
        note(primed, QcEntityKind::PrimedExplosive);
    }
    for (const QcArmourStand &stand : entities.stands().stands()) {
        note(stand, QcEntityKind::ArmourStand);
    }
    for (const QcMovingBlock &moving : entities.movingBlocks().all()) {
        note(moving, QcEntityKind::MovingBlock);
    }
    for (const QcMinecart &cart : entities.minecarts().all()) {
        note(cart, QcEntityKind::Minecart);
    }
    for (const QcExperienceOrb &orb : entities.orbs().orbs()) {
        note(orb, QcEntityKind::ExperienceOrb);
    }
    for (const QcProjectile &shot : entities.projectiles().all()) {
        note(shot, QcEntityKind::Projectile);
    }
    for (const QcHangingEntity &hanging : entities.hangings().all()) {
        note(hanging, QcEntityKind::Hanging);
    }
    for (const QcBoat &boat : entities.boats().all()) {
        note(boat, QcEntityKind::Boat);
    }
    // Every client that became a player, also one that is dead or leaving.
    for (const std::unique_ptr<QcServerClient> &client : entities.clients()) {
        if (client->id != 0) {
            note(*client, QcEntityKind::Player);
        }
    }
    return problems;
}

template<typename Entry, typename Position>
[[nodiscard]] QStringList checkChunks(const QList<QcEntityRecord::Ordered<Entry>> &entries, QcChunkPos chunk, const char *kind, Position position)
{
    QStringList problems;
    for (const QcEntityRecord::Ordered<Entry> &ordered : entries) {
        if (QcPhysics::blockContaining(position(ordered.entry)).chunk() != chunk) {
            problems.append(u"a saved %1 (order %2) is in the record of chunk %3, %4 but not in that chunk"_s.arg(QLatin1StringView(kind))
                                .arg(ordered.order)
                                .arg(chunk.x)
                                .arg(chunk.z));
        }
    }
    return problems;
}

// Every rider is a player who plays and lives, in one cart only (decision E-382).
[[nodiscard]] QStringList checkRiders(const QcEntities &entities)
{
    QStringList problems;
    QSet<quint32> riders;
    for (const QcMinecart &cart : entities.minecarts().all()) {
        if (cart.rider == 0) {
            continue;
        }
        const bool player = std::ranges::any_of(entities.clients(), [&cart](const std::unique_ptr<QcServerClient> &client) {
            return client->id == cart.rider && client->state == QcServerClient::State::Playing && !client->dead;
        });
        if (!player) {
            problems.append(u"minecart %1 is ridden by %2, who is no living player"_s.arg(cart.id).arg(cart.rider));
        }
        if (riders.contains(cart.rider)) {
            problems.append(u"player %1 rides two minecarts"_s.arg(cart.rider));
        }
        riders.insert(cart.rider);
    }
    return problems;
}

} // namespace

QStringList problems(const QcEntities &entities)
{
    QStringList problems;
    problems.append(checkIdsAndTables(entities));
    problems.append(checkRiders(entities));

    const QHash<QcChunkPos, QcEntityRecord> saved = QcEntityRecords::collect(entities);
    for (const QcEntityRecord &record : saved) {
        problems.append(checkChunks(record.creatures, record.chunk, "creature", [](const QcMobManager::Stored &stored) { return stored.position; }));
        problems.append(checkChunks(record.items, record.chunk, "item", [](const QcItemEntityManager::Drop &drop) { return drop.position; }));
        problems.append(checkChunks(record.fallingBlocks, record.chunk, "falling block",
                                    [](const QcFallingBlock &falling) { return falling.motion.position; }));
        problems.append(checkChunks(record.primedExplosives, record.chunk, "lit explosive",
                                    [](const QcPrimedExplosive &primed) { return primed.motion.position; }));
        problems.append(checkChunks(record.stands, record.chunk, "armour stand", [](const QcArmourStand &stand) { return stand.motion.position; }));
        problems.append(checkChunks(record.minecarts, record.chunk, "minecart", [](const QcMinecart &cart) { return cart.motion.position; }));
        problems.append(checkChunks(record.orbs, record.chunk, "experience orb", [](const QcExperienceOrb &orb) { return orb.motion.position; }));
        problems.append(checkChunks(record.projectiles, record.chunk, "projectile", [](const QcProjectile &shot) { return shot.motion.position; }));
        problems.append(checkChunks(record.hangings, record.chunk, "frame or painting", [](const QcHangingEntity &hanging) { return hanging.motion.position; }));
        problems.append(checkChunks(record.boats, record.chunk, "boat", [](const QcBoat &boat) { return boat.motion.position; }));
    }
    return problems;
}

} // namespace QcEntityChecks
