#include "QcEntitySync.h"

#include "QcServerState.h"

namespace {

quint8 flagsOf(const QcEntityMotion &motion, bool swung, bool sneaking, bool sprinting)
{
    return quint8((swung ? QcMsg::Swing : 0) | (motion.onGround ? QcMsg::OnGround : 0) | (motion.inWater ? QcMsg::InWater : 0)
                  | (sneaking ? QcMsg::Sneaking : 0) | (sprinting ? QcMsg::Sprinting : 0));
}

} // namespace

QcEntitySync::QcEntitySync(QcServerState &state)
    : m_state(state)
{
}

void QcEntitySync::sync(QcServerClient &client)
{
    Pass pass{client, {}, {}};
    for (const QcMobManager::Mob &mob : m_state.mobs.mobs()) {
        if (mob.dormant) {
            continue;
        }

        consider(pass,
                 {mob.id, mob.type, QString(), mob.motion.position, mob.yaw, mob.health, {}, {}, 0.0f,
                  flagsOf(mob.motion, mob.attackedThisTick, false, false), qcNoItem},
                 mob.hurtThisTick);
    }

    considerPlayers(pass);
    considerFallingBlocks(pass);
    considerItems(pass);
    considerStands(pass);
    removeUnseen(pass);

    if (!pass.moves.moves.isEmpty()) {
        client.connection->send(pass.moves);
    }
}

void QcEntitySync::consider(Pass &pass, const QcMsg::EntitySpawn &spawn, bool hurt)
{
    QcServerClient &client = pass.client;
    if ((spawn.position - client.motion.position).length() > entityViewDistance) {
        return;
    }

    pass.visible.insert(spawn.id);
    const bool known = client.knownEntities.contains(spawn.id);
    if (!known || client.staleEntities.remove(spawn.id)) {
        client.knownEntities.insert(spawn.id);
        client.connection->send(spawn);
    }

    // A known entity sent again (new look) still moves, swings and flashes in this tick.
    if (known) {
        pass.moves.moves.append({spawn.id, spawn.position, spawn.yaw, spawn.health, hurt, spawn.pitch, spawn.flags, spawn.held});
    }
}

void QcEntitySync::considerPlayers(Pass &pass)
{
    for (const auto &other : m_state.clients) {
        if (other.get() != &pass.client && other->state == QcServerClient::State::Playing && !other->dead) {
            const QcItemStack &held = other->inventory.selectedStack();
            QcMsg::EntitySpawn spawn{other->entityId, QcMsg::EntitySpawn::playerType, other->name, other->motion.position, other->yaw,
                                     other->health, {}, other->appearance, other->pitch,
                                     flagsOf(other->motion, other->swung, other->sneaking, other->sprinting), held.isEmpty() ? qcNoItem : held.item};
            spawn.equipment = other->equipment;
            consider(pass, spawn, other->hurtThisTick);
        }
    }
}

void QcEntitySync::considerFallingBlocks(Pass &pass)
{
    for (const QcFallingBlockManager::Falling &falling : m_state.falling.all()) {
        QcMsg::EntitySpawn spawn{falling.id, QcMsg::EntitySpawn::fallingBlockType, QString(), falling.motion.position, 0.0f, 0.0,
                                 {}, {}, 0.0f, 0, qcNoItem};
        spawn.block = falling.block;
        consider(pass, spawn, false);
    }
}

void QcEntitySync::considerItems(Pass &pass) const
{
    QcServerClient &client = pass.client;
    // Items are sent again when their count changed and moved only when their position did.
    for (const QcItemEntityManager::Item &item : m_state.items.items()) {
        if (item.dormant || (item.motion.position - client.motion.position).length() > itemViewDistance) {
            continue;
        }

        pass.visible.insert(item.id);

        const auto known = client.knownItems.find(item.id);
        if (known == client.knownItems.end() || known->count != item.stack.count) {
            client.connection->send(QcMsg::EntitySpawn{item.id, QcMsg::EntitySpawn::itemType, QString(), item.motion.position, 0.0f,
                                                       0.0, item.stack, {}, 0.0f, 0, qcNoItem});
            client.knownEntities.insert(item.id);
            client.knownItems.insert(item.id, {item.stack.count, item.motion.position});
        } else if ((known->position - item.motion.position).length() > itemMoveThreshold) {
            pass.moves.moves.append({item.id, item.motion.position, 0.0f, 0.0, false, 0.0f, 0, qcNoItem});
            known->position = item.motion.position;
        }
    }
}

void QcEntitySync::considerStands(Pass &pass) const
{
    QcServerClient &client = pass.client;
    for (const QcArmourStandManager::Stand &stand : m_state.stands.stands()) {
        if ((stand.position - client.motion.position).length() > entityViewDistance) {
            continue;
        }

        pass.visible.insert(stand.id);
        if (client.knownEntities.contains(stand.id) && !client.staleEntities.remove(stand.id)) {
            continue;
        }

        QcMsg::EntitySpawn spawn{stand.id, QcMsg::EntitySpawn::armourStandType, stand.name, stand.position, stand.yaw, 0.0,
                                 {}, {}, 0.0f, 0, stand.held.isEmpty() ? qcNoItem : stand.held.item};
        spawn.equipment = stand.equipment;
        spawn.stand = stand.pose;
        spawn.appearance.cape = stand.cape;
        client.knownEntities.insert(stand.id);
        client.connection->send(spawn);
    }
}

void QcEntitySync::removeUnseen(Pass &pass) const
{
    QcServerClient &client = pass.client;
    for (auto it = client.knownEntities.begin(); it != client.knownEntities.end();) {
        if (!pass.visible.contains(*it)) {
            client.connection->send(QcMsg::EntityRemove{*it, m_state.died.contains(*it)});
            client.knownItems.remove(*it);
            client.staleEntities.remove(*it);
            it = client.knownEntities.erase(it);
        } else {
            ++it;
        }
    }
}
