#include "server/core/QcEntitySync.h"

#include "server/core/QcServerState.h"
#include "server/villages/QcVillagerLooks.h"

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

// What a creature holds: a drink to heal while it drinks (decision E-623), else the block an
// enderman carries (decision E-460), else its kind's item (an archer's bow, decision E-426).
QcItemId heldBy(const QcCreature &creature, const QcMobDefinition &kind)
{
    if (kind.drinks && creature.archer.has() && creature.archer->drinking()) {
        return kind.drinks->itemId;
    }
    return creature.carriedItem != qcNoItem ? creature.carriedItem : kind.heldItemId;
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
        const quint16 breeding = quint16((creature.isYoung() ? QcMsg::Young : 0) | (creature.love > 0.0 ? QcMsg::InLove : 0)
                                         | QcMsg::poseFlags(creature.pose));
        now.yaw = creature.yaw;
        now.health = creature.health;
        now.hurt = creature.hurtThisTick;
        now.flags = quint16(flagsOf(creature, creature.attackedThisTick, false, false) | breeding);
        now.held = heldBy(creature, m_state.content().mobs()[creature.type]);
        if (creature.archer.has() && creature.archer->drawing()) {
            now.flags = quint16(now.flags | QcMsg::Drawing);
        }
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
        if (player.drawing) {
            now.flags = quint16(now.flags | QcMsg::Drawing);
        }
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
    case QcEntityKind::Projectile: {
        // Turned along its flight; a stuck one keeps where it points (decision E-425).
        const auto &projectile = entity.as<QcProjectile>();
        now.yaw = projectile.yaw;
        now.pitch = projectile.pitch;
        now.flags = projectile.stuck ? quint16(QcMsg::Stuck) : quint16(0);
        break;
    }
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
    case QcEntityKind::Creature: {
        const auto &creature = entity.as<QcCreature>();
        spawn.creatureType = creature.type;
        if (creature.isVillager()) {
            spawn.appearance = QcVillagerLooks::appearanceOf(m_state.content(), creature.type, *creature.villager);
        }
        // A tame creature's collar is drawn in the shirt colour (QcTint::Collar, decision E-480); a
        // wild one of a kind with a collar (a summoned dog) wears its kind's default colour.
        if (creature.isTame()) {
            spawn.appearance.shirtColor = creature.tame->collar;
        } else if (const auto &tameable = m_state.content().mobs()[creature.type].tameable; tameable && tameable->hasCollar()) {
            spawn.appearance.shirtColor = tameable->collarColor(tameable->collarDefault).value_or(0);
        }
        break;
    }
    case QcEntityKind::ExperienceOrb:
        break;
    case QcEntityKind::Projectile: {
        // The client draws it as its item (an arrow).
        const QcProjectileDefinition *definition = m_state.content().projectile(entity.as<QcProjectile>().type);
        if (const std::optional<QcItemId> item = definition ? m_state.content().itemId(definition->item) : std::nullopt) {
            spawn.item = {*item, 1, 0};
        }
        break;
    }
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
    // Projectiles count whether they stick, so sticking and falling again send them anew.
    const bool projectile = entity.kind == QcEntityKind::Projectile;
    const int count = orb ? entity.as<QcExperienceOrb>().value
                      : item ? entity.as<QcItemEntity>().stack.count
                             : projectile && entity.as<QcProjectile>().stuck ? 1 : 0;
    const auto known = client.view.known.find(entity.id);
    if (known == client.view.known.end()) {
        client.view.known.insert(entity.id, {count, now.position});
        client.connection->send(spawnOf(entity, now));
        return;
    }

    switch (entity.kind) {
    case QcEntityKind::Item:
    case QcEntityKind::ExperienceOrb:
    case QcEntityKind::Projectile:
        // Sent again when the count (a projectile: whether it sticks) changed, moved only when the
        // position did.
        if (known->count != count && projectile) {
            // Stuck or falling again: the new state, a move to where it is and a stop sample after it.
            *known = {count, now.position, true};
            client.connection->send(spawnOf(entity, now));
            pass.moves.moves.append(now);
        } else if (known->count != count) {
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
