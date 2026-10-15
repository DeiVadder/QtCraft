#include "QcClientEntities.h"

#include "QcContentRegistry.h"
#include "QcPhysics.h"

#include <algorithm>

namespace {

// The creature type of a spawn; 0 for the other kinds.
int creatureTypeOf(const QcMsg::EntitySpawn &spawn)
{
    return spawn.kind == QcEntityKind::Creature ? spawn.creatureType : 0;
}

bool isBlockKind(QcEntityKind kind)
{
    return kind == QcEntityKind::FallingBlock || kind == QcEntityKind::MovingBlock;
}

// Whether the server sent a player, a stand, a minecart, a known creature, a valid item or a valid falling or
// moving block (the decoder already refused unknown kinds).
bool isKnownEntityType(const QcMsg::EntitySpawn &spawn, const QcContentRegistry *content)
{
    switch (spawn.kind) {
    case QcEntityKind::Creature:
        return content && spawn.creatureType >= 0 && spawn.creatureType < content->mobs().size();
    case QcEntityKind::Item:
        return content && !spawn.item.isEmpty() && content->isValidItem(spawn.item.item);
    case QcEntityKind::FallingBlock:
    case QcEntityKind::MovingBlock:
        return content && content->isValidBlock(spawn.block) && spawn.block != qcAirBlockId;
    case QcEntityKind::Player:
    case QcEntityKind::ArmourStand:
    case QcEntityKind::Minecart:
        return true;
    }
    return false;
}

// Armour only for players and armour stands and only of items the content knows (decisions E-232, E-238).
QcEquipment equipmentOf(const QcMsg::EntitySpawn &spawn, const QcContentRegistry *content)
{
    const bool wears = spawn.kind == QcEntityKind::Player || spawn.kind == QcEntityKind::ArmourStand;
    return wears && content ? spawn.equipment.knownTo(*content) : QcEquipment();
}

QcStandPose standOf(const QcMsg::EntitySpawn &spawn)
{
    return spawn.kind == QcEntityKind::ArmourStand ? spawn.stand : QcStandPose();
}

QcClientEntity entityFromSpawn(const QcMsg::EntitySpawn &spawn, const QcContentRegistry *content)
{
    // Items and minecarts (what the cart carries, decision E-386).
    const bool item = spawn.kind == QcEntityKind::Item || (spawn.kind == QcEntityKind::Minecart && content && content->isValidItem(spawn.item.item));

    QcClientEntity entity;
    entity.item = item ? spawn.item : QcItemStack();
    entity.appearance = spawn.appearance;
    entity.equipment = equipmentOf(spawn, content);
    entity.stand = standOf(spawn);
    entity.pitch = entity.previousPitch = spawn.pitch;
    entity.flags = spawn.flags;
    entity.held = spawn.held;
    entity.id = spawn.id;
    entity.kind = spawn.kind;
    entity.creatureType = creatureTypeOf(spawn);
    entity.block = isBlockKind(spawn.kind) ? spawn.block : qcAirBlockId;
    entity.name = spawn.name;
    entity.position = entity.previousPosition = spawn.position;
    entity.yaw = entity.previousYaw = spawn.yaw;
    entity.health = spawn.health;
    return entity;
}

} // namespace

bool QcClientEntities::spawn(const QcMsg::EntitySpawn &spawn, const QcContentRegistry *content)
{
    if (!isKnownEntityType(spawn, content)) {
        return false;
    }

    // Items and minecarts (what the cart carries, decision E-386).
    const bool item = spawn.kind == QcEntityKind::Item || (spawn.kind == QcEntityKind::Minecart && content && content->isValidItem(spawn.item.item));
    // A known entity sent again changed its count (items) or look (players); keep its motion history.
    const auto known = m_entities.find(spawn.id);
    if (known != m_entities.end() && known->kind == spawn.kind && known->creatureType == creatureTypeOf(spawn)) {
        known->item = item ? spawn.item : QcItemStack();
        known->appearance = spawn.appearance;
        known->equipment = equipmentOf(spawn, content);
        known->stand = standOf(spawn);
        known->held = spawn.held;
        known->name = spawn.name;
        if (known->isArmourStand()) {
            // A stand turned by a command; it never moves otherwise.
            known->yaw = known->previousYaw = spawn.yaw;
        }
        return true;
    }

    const QcClientEntity entity = entityFromSpawn(spawn, content);
    m_entities.insert(entity.id, entity);
    // The motion starts where it appeared, at about the time it appeared.
    m_tracks.remove(entity.id);
    if (m_clock.latestServerMs() >= 0) {
        m_tracks[entity.id].push({double(m_clock.latestServerMs()), spawn.position, spawn.yaw, spawn.pitch}, m_clock.intervalMs());
    }
    return true;
}

void QcClientEntities::moves(const QcMsg::EntityMoves &moves, qint64 localMs)
{
    m_clock.observe(moves.timeMs, localMs);
    for (const QcMsg::EntityMove &move : moves.moves) {
        applyMove(move, double(moves.timeMs));
    }
}

void QcClientEntities::applyMove(const QcMsg::EntityMove &move, double serverMs)
{
    const auto it = m_entities.find(move.id);
    if (it == m_entities.end()) {
        return;
    }

    m_tracks[move.id].push({serverMs, move.position, move.yaw, move.pitch}, m_clock.intervalMs());
    it->health = move.health;
    it->flags = move.flags;
    it->held = move.held;

    if ((move.flags & QcMsg::Swing) != 0) {
        it->swingTicks = 0;
    }
    if (move.hurt) {
        it->hurtTicks = hurtFlashTicks;
    }
}

void QcClientEntities::remove(QcMsg::EntityRemove removed)
{
    m_tracks.remove(removed.id);
    QcClientEntity gone = m_entities.take(removed.id);
    if (removed.died && gone.id == removed.id && !gone.isItem()) {
        gone.deathTicks = 0;
        gone.previousPosition = gone.position;
        m_dying.insert(gone.id, gone);
    }
}

void QcClientEntities::tick(qint64 localMs)
{
    const std::optional<double> shown = m_clock.advance(localMs);
    for (QcClientEntity &entity : m_entities) {
        advancePose(entity, shown);
        entity.hurtTicks = std::max(0, entity.hurtTicks - 1);
        if (entity.swingTicks >= 0) {
            ++entity.swingTicks;
            if (entity.swingTicks >= swingTicks) {
                entity.swingTicks = -1;
            }
        }
    }

    for (QcClientEntity &dying : m_dying) {
        ++dying.deathTicks;
    }
    m_dying.removeIf([](const auto &entry) { return entry.value().deathTicks >= deathTicks; });
}

void QcClientEntities::advancePose(QcClientEntity &entity, std::optional<double> shown)
{
    entity.previousPosition = entity.position;
    entity.previousYaw = entity.yaw;
    entity.previousPitch = entity.pitch;

    // Frames interpolate from the start of the tick to its end, so the tick's end shows the pose
    // one tick after the shown moment.
    const auto track = m_tracks.find(entity.id);
    if (track != m_tracks.end() && !track->isEmpty()) {
        const bool buffered = m_buffered && shown;
        const QcSnapshotBuffer::Snapshot pose = buffered ? track->at(*shown + QcPhysics::tickDuration * 1000.0) : track->newest();
        if (buffered) {
            track->dropBefore(*shown);
        }
        entity.position = pose.position;
        entity.yaw = pose.yaw;
        entity.pitch = pose.pitch;
    }

    // A teleport shows at once instead of sweeping through everything in between.
    if ((entity.position - entity.previousPosition).length() > QcSnapshotBuffer::teleportMetres) {
        entity.previousPosition = entity.position;
        entity.previousYaw = entity.yaw;
        entity.previousPitch = entity.pitch;
    }

    entity.lastStep = std::hypot(entity.position.x - entity.previousPosition.x, entity.position.z - entity.previousPosition.z);
    entity.walkDistance += entity.lastStep;
    entity.speed += (entity.lastStep * QcPhysics::tickRate - entity.speed) * speedSmoothing;
}
