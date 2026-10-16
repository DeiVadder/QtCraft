#include "QcEntitySync.h"

#include "QcServerState.h"

#include <algorithm>

namespace {

quint16 flagsOf(const QcEntity &entity, bool swung, bool sneaking, bool sprinting)
{
    const QcEntityMotion &motion = entity.motion;
    // Sneaking in flight is descending: the figure does not crouch (decision E-284).
    const bool crouching = sneaking && !motion.flying;
    return quint16((swung ? QcMsg::Swing : 0) | (motion.onGround ? QcMsg::OnGround : 0) | (motion.inWater ? QcMsg::InWater : 0)
                   | (crouching ? QcMsg::Sneaking : 0) | (sprinting ? QcMsg::Sprinting : 0) | (entity.burnTicks > 0 ? QcMsg::Burning : 0));
}

} // namespace

QcEntitySync::QcEntitySync(QcServerState &state)
    : m_state(state)
{
}

void QcEntitySync::sync(QcServerClient &client)
{
    Pass pass{client, {}, {}};
    std::as_const(m_state.entities()).forEach([this, &pass](const QcEntity &entity) { consider(pass, entity); });
    removeUnseen(pass);

    if (!pass.moves.moves.isEmpty()) {
        pass.moves.timeMs = m_state.clock().playerTimeNs() / 1'000'000;
        client.connection->send(pass.moves);
    }
}

QcMsg::EntityMove QcEntitySync::seen(const QcEntity &entity) const
{
    QcMsg::EntityMove now{entity.id, entity.motion.position, 0.0f, 0.0, false, 0.0f, 0, qcNoItem};
    switch (entity.kind) {
    case QcEntityKind::Creature: {
        const auto &creature = entity.as<QcCreature>();
        const quint16 breeding = quint16((creature.isYoung() ? QcMsg::Young : 0) | (creature.love > 0.0 ? QcMsg::InLove : 0));
        now.yaw = creature.yaw;
        now.health = creature.health;
        now.hurt = creature.hurtThisTick;
        now.flags = quint16(flagsOf(creature, creature.attackedThisTick, false, false) | breeding);
        break;
    }
    case QcEntityKind::Player: {
        const auto &player = entity.as<QcServerClient>();
        const QcItemStack &held = player.inventory.selectedStack();
        now.yaw = player.yaw;
        now.health = player.health;
        now.hurt = player.hurtThisTick;
        now.pitch = player.pitch;
        now.flags = flagsOf(player, player.swung, player.sneaking, player.sprinting);
        if (m_state.entities().minecarts().ridden(player.id)) {
            now.flags = quint16((now.flags & ~QcMsg::Sneaking) | QcMsg::Riding);
        }
        now.held = held.isEmpty() ? qcNoItem : held.item;
        break;
    }
    case QcEntityKind::Minecart:
        now.yaw = entity.yaw;
        break;
    case QcEntityKind::MovingBlock:
        now.position = QcMovingBlocks::positionAt(entity.as<QcMovingBlock>(), m_state.clock().tick());
        break;
    case QcEntityKind::ArmourStand: {
        const auto &stand = entity.as<QcArmourStand>();
        now.yaw = stand.yaw;
        now.held = stand.held.isEmpty() ? qcNoItem : stand.held.item;
        break;
    }
    case QcEntityKind::ExperienceOrb:
        // The client draws an orb by its value (decision E-398).
        now.health = entity.as<QcExperienceOrb>().value;
        break;
    case QcEntityKind::Item:
    case QcEntityKind::FallingBlock:
        break;
    }
    return now;
}

QcMsg::EntitySpawn QcEntitySync::spawnOf(const QcEntity &entity, const QcMsg::EntityMove &now) const
{
    QcMsg::EntitySpawn spawn{entity.id, entity.kind, QString(), now.position, now.yaw, now.health, {}, {}, now.pitch, now.flags, now.held};
    switch (entity.kind) {
    case QcEntityKind::Player: {
        const auto &player = entity.as<QcServerClient>();
        spawn.name = player.name;
        spawn.appearance = player.appearance;
        spawn.equipment = player.equipment;
        break;
    }
    case QcEntityKind::Item:
        spawn.item = entity.as<QcItemEntity>().stack;
        break;
    case QcEntityKind::FallingBlock:
        spawn.block = entity.as<QcFallingBlock>().block;
        break;
    case QcEntityKind::MovingBlock:
        spawn.block = entity.as<QcMovingBlock>().block;
        break;
    case QcEntityKind::ArmourStand: {
        const auto &stand = entity.as<QcArmourStand>();
        spawn.name = stand.name;
        spawn.equipment = stand.equipment;
        spawn.stand = stand.pose;
        spawn.appearance.cape = stand.cape;
        break;
    }
    case QcEntityKind::Creature:
        spawn.creatureType = entity.as<QcCreature>().type;
        break;
    case QcEntityKind::ExperienceOrb:
        break;
    case QcEntityKind::Minecart:
        // The cart's item tells what it carries (QcItemDefinition::cartBlock).
        if (const std::optional<QcItemId> item = m_state.content().itemId(entity.as<QcMinecart>().item)) {
            spawn.item = {*item, 1, 0};
        }
        break;
    }
    return spawn;
}

void QcEntitySync::consider(Pass &pass, const QcEntity &entity) const
{
    QcServerClient &client = pass.client;
    // Items and orbs: small, many, seen from closer and sent again when their count or value changed.
    const bool orb = entity.kind == QcEntityKind::ExperienceOrb;
    const bool item = entity.kind == QcEntityKind::Item || orb;
    if (&entity == &client || entity.dormant || (item && !orb && entity.as<QcItemEntity>().surplus)) {
        return;
    }

    const QcVec3 position = entity.kind == QcEntityKind::MovingBlock
                                ? QcMovingBlocks::positionAt(entity.as<QcMovingBlock>(), m_state.clock().tick())
                                : entity.motion.position;
    if ((position - client.motion.position).length() > (item ? itemViewDistance : entityViewDistance)) {
        return;
    }

    const QcMsg::EntityMove now = seen(entity);

    pass.visible.insert(entity.id);
    const int count = orb ? entity.as<QcExperienceOrb>().value : item ? entity.as<QcItemEntity>().stack.count : 0;
    const auto known = client.view.known.find(entity.id);
    if (known == client.view.known.end()) {
        client.view.known.insert(entity.id, {count, now.position});
        client.connection->send(spawnOf(entity, now));
        return;
    }

    switch (entity.kind) {
    case QcEntityKind::Item:
    case QcEntityKind::ExperienceOrb:
        // Sent again when the count changed, moved only when the position did.
        if (known->count != count) {
            *known = {count, now.position, false};
            client.connection->send(spawnOf(entity, now));
        } else {
            sendChangedMove(pass, *known, now, itemMoveThreshold);
        }
        break;
    case QcEntityKind::Minecart:
        // A cart standing still sends nothing.
        sendChangedMove(pass, *known, now, cartMoveThreshold);
        break;
    case QcEntityKind::ArmourStand:
        // Stands send no moves: a spawn when they changed or were pushed.
        if (client.view.stale.remove(entity.id)) {
            client.connection->send(spawnOf(entity, now));
        }
        break;
    default:
        // A known entity sent again (new look) still moves, swings and flashes in this tick.
        if (client.view.stale.remove(entity.id)) {
            client.connection->send(spawnOf(entity, now));
        }
        pass.moves.moves.append(now);
        break;
    }
}

void QcEntitySync::sendChangedMove(Pass &pass, QcServerClient::SentEntity &known, const QcMsg::EntityMove &now, double threshold)
{
    if ((known.position - now.position).length() > threshold) {
        known = {known.count, now.position, true};
        pass.moves.moves.append(now);
        return;
    }

    // Came to rest: one more move where it stays (a stop sample), so the client ends its motion
    // there instead of continuing the last step past the newest snapshot (QcSnapshotBuffer).
    if (known.moving) {
        known = {known.count, now.position, false};
        pass.moves.moves.append(now);
    }
}

void QcEntitySync::removeUnseen(Pass &pass) const
{
    QcServerClient &client = pass.client;
    QList<quint32> gone;
    for (auto it = client.view.known.begin(); it != client.view.known.end();) {
        if (!pass.visible.contains(it.key())) {
            gone.append(it.key());
            client.view.stale.remove(it.key());
            it = client.view.known.erase(it);
        } else {
            ++it;
        }
    }

    // In the order of the ids, so the removals of a tick reach the client in the same order every time.
    std::ranges::sort(gone);
    for (const quint32 id : std::as_const(gone)) {
        client.connection->send(QcMsg::EntityRemove{id, m_state.entities().diedThisTick(id)});
    }
}
