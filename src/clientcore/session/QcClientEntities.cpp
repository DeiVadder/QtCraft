#include "QcClientEntities.h"

#include "QcContentRegistry.h"
#include "QcPhysics.h"

#include <algorithm>

namespace {

// Whether the server sent a player, a known mob, a valid item or a valid falling block.
bool isKnownEntityType(const QcMsg::EntitySpawn &spawn, const QcContentRegistry *content)
{
    const bool item = spawn.mobType == QcMsg::EntitySpawn::itemType;
    const bool fallingBlock = spawn.mobType == QcMsg::EntitySpawn::fallingBlockType;
    return !((spawn.mobType < QcMsg::EntitySpawn::playerType && !item && !fallingBlock)
             || (fallingBlock && (!content || !content->isValidBlock(spawn.block) || spawn.block == qcAirBlockId))
             || (spawn.mobType >= 0 && (!content || spawn.mobType >= content->mobs().size()))
             || (item && (!content || spawn.item.isEmpty() || !content->isValidItem(spawn.item.item))));
}

// Armour only for players and only of items the content knows (decision E-232).
QcEquipment equipmentOf(const QcMsg::EntitySpawn &spawn, const QcContentRegistry *content)
{
    return spawn.mobType == QcMsg::EntitySpawn::playerType && content ? spawn.equipment.knownTo(*content) : QcEquipment();
}

QcClientEntity entityFromSpawn(const QcMsg::EntitySpawn &spawn, const QcContentRegistry *content)
{
    const bool item = spawn.mobType == QcMsg::EntitySpawn::itemType;
    const bool fallingBlock = spawn.mobType == QcMsg::EntitySpawn::fallingBlockType;

    QcClientEntity entity;
    entity.item = item ? spawn.item : QcItemStack();
    entity.appearance = spawn.appearance;
    entity.equipment = equipmentOf(spawn, content);
    entity.pitch = entity.previousPitch = spawn.pitch;
    entity.flags = spawn.flags;
    entity.held = spawn.held;
    entity.id = spawn.id;
    entity.mobType = spawn.mobType;
    entity.block = fallingBlock ? spawn.block : qcAirBlockId;
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

    const bool item = spawn.mobType == QcMsg::EntitySpawn::itemType;
    // A known entity sent again changed its count (items) or look (players); keep its motion history.
    if (const auto known = m_entities.find(spawn.id); known != m_entities.end() && known->mobType == spawn.mobType) {
        known->item = item ? spawn.item : QcItemStack();
        known->appearance = spawn.appearance;
        known->equipment = equipmentOf(spawn, content);
        return true;
    }

    const QcClientEntity entity = entityFromSpawn(spawn, content);
    m_entities.insert(entity.id, entity);
    return true;
}

void QcClientEntities::move(const QcMsg::EntityMove &move)
{
    const auto it = m_entities.find(move.id);
    if (it == m_entities.end()) {
        return;
    }

    it->previousPosition = it->position;
    it->previousYaw = it->yaw;
    it->previousPitch = it->pitch;
    it->lastStep = std::hypot(move.position.x - it->position.x, move.position.z - it->position.z);
    it->walkDistance += it->lastStep;
    it->stepThisTick += it->lastStep;
    it->position = move.position;
    it->yaw = move.yaw;
    it->pitch = move.pitch;
    it->health = move.health;
    it->flags = move.flags;
    it->held = move.held;
    it->moved = true;

    if ((move.flags & QcMsg::Swing) != 0) {
        it->swingTicks = 0;
    }
    if (move.hurt) {
        it->hurtTicks = hurtFlashTicks;
    }
}

void QcClientEntities::remove(QcMsg::EntityRemove removed)
{
    QcClientEntity gone = m_entities.take(removed.id);
    if (removed.died && gone.id == removed.id && !gone.isItem()) {
        gone.deathTicks = 0;
        gone.previousPosition = gone.position;
        m_dying.insert(gone.id, gone);
    }
}

void QcClientEntities::tick()
{
    for (QcClientEntity &entity : m_entities) {
        entity.hurtTicks = std::max(0, entity.hurtTicks - 1);
        entity.speed += (entity.stepThisTick * QcPhysics::tickRate - entity.speed) * speedSmoothing;
        entity.stepThisTick = 0.0;

        // Items move only on change, and a tick may pass without a mob or player move: then the
        // entity stays where it is (no interpolation towards an old position).
        if (!entity.moved) {
            entity.lastStep = 0.0;
            entity.previousPosition = entity.position;
            entity.previousYaw = entity.yaw;
            entity.previousPitch = entity.pitch;
        }
        entity.moved = false;

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
