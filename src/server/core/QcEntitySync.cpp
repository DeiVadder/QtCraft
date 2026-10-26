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
    return static_cast<quint16>((swung ? QcMsg::Swing : 0) | (motion.onGround ? QcMsg::OnGround : 0) | (motion.inWater ? QcMsg::InWater : 0)
                   | (crouching ? QcMsg::Sneaking : 0) | (sprinting ? QcMsg::Sprinting : 0) | (entity.burnTicks > 0 ? QcMsg::Burning : 0));
}

// What a creature holds: a drink to heal while it drinks (decision E-623), else the block a
// shadewalker carries (decision E-460), else its kind's item (an archer's bow, decision E-426).
QcItemId heldBy(const QcCreature &creature, const QcMobDefinition &kind)
{
    if (kind.drinks && creature.archer.has() && creature.archer->drinking()) {
        return kind.drinks->itemId;
    }
    return creature.carriedItem != qcNoItem ? creature.carriedItem : kind.heldItemId;
}

// The move of a creature: `now` with its turn, health, flags and what it holds.
QcMsg::EntityMove creatureMove(QcMsg::EntityMove now, const QcCreature &creature, const QcMobDefinition &kind)
{
    const quint16 breeding = static_cast<quint16>((creature.isYoung() ? QcMsg::Young : 0) | (creature.love > 0.0 ? QcMsg::InLove : 0)
                                     | QcMsg::poseFlags(creature.pose));
    now.yaw = creature.yaw;
    now.health = creature.health;
    now.hurt = creature.hurtThisTick;
    now.flags = static_cast<quint16>(flagsOf(creature, creature.attackedThisTick, false, false) | breeding);
    now.held = heldBy(creature, kind);
    if (creature.archer.has() && creature.archer->drawing()) {
        now.flags = static_cast<quint16>(now.flags | QcMsg::Drawing);
    }
    return now;
}

// The look a creature is drawn with: a villager's clothes (decision E-411) and the collar colour in
// the shirt colour (QcTint::Collar, decision E-480): a tame creature's own, a wild one of a kind with
// a collar (a summoned dog) its kind's default.
QcAppearance creatureLook(const QcCreature &creature, const QcContentRegistry &content)
{
    QcAppearance look;
    if (creature.isVillager()) {
        look = QcVillagerLooks::appearanceOf(content, creature.type, *creature.villager);
    }
    if (creature.isTame()) {
        look.shirtColor = creature.tame->collar;
    } else if (const auto &tameable = content.mobs()[creature.type].tameable; tameable && tameable->hasCollar()) {
        look.shirtColor = tameable->collarColor(tameable->collarDefault).value_or(0);
    }
    return look;
}

// The move of a player: `now` with the view, health, flags (`riding` a minecart) and the selected item.
QcMsg::EntityMove playerMove(QcMsg::EntityMove now, const QcServerClient &player, bool riding)
{
    const QcItemStack &held = player.inventory.selectedStack();
    now.yaw = player.yaw;
    now.health = player.health;
    now.hurt = player.hurtThisTick;
    now.pitch = player.pitch;
    now.flags = flagsOf(player, player.swung, player.sneaking, player.sprinting);
    if (riding) {
        now.flags = static_cast<quint16>((now.flags & ~QcMsg::Sneaking) | QcMsg::Riding);
    }
    now.held = held.isEmpty() ? qcNoItem : held.item;
    if (player.drawing) {
        now.flags = static_cast<quint16>(now.flags | QcMsg::Drawing);
    }
    return now;
}

} // namespace

QcEntitySync::QcEntitySync(QcServerState &state)
    : m_state(state)
{
}

void QcEntitySync::sync(QcServerClient &client)
{
    Pass pass{*this, client, {}, {}};
    std::as_const(m_state.entities()).forEach([&pass](const QcEntity &entity) { pass.consider(entity); });
    pass.removeUnseen();

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
        now = creatureMove(now, creature, m_state.content().mobs()[creature.type]);
        break;
    }
    case QcEntityKind::Player: {
        const auto &player = entity.as<QcServerClient>();
        now = playerMove(now, player, m_state.entities().minecarts().ridden(player.id));
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
        now.flags = projectile.stuck ? static_cast<quint16>(QcMsg::Stuck) : quint16{0};
        break;
    }
    case QcEntityKind::PrimedExplosive:
        // The client flashes it by the ticks left (decision E-604).
        now.health = entity.as<QcPrimedExplosive>().fuse;
        break;
    case QcEntityKind::Item:
    case QcEntityKind::FallingBlock:
    case QcEntityKind::Hanging:
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
        // Only what draws it: a dropped backpack keeps its contents to the server (decision E-832).
        spawn.item = entity.as<QcItemEntity>().stack.forDisplay();
        break;
    case QcEntityKind::FallingBlock:
        spawn.block = entity.as<QcFallingBlock>().block;
        break;
    case QcEntityKind::MovingBlock:
        spawn.block = entity.as<QcMovingBlock>().block;
        break;
    case QcEntityKind::PrimedExplosive:
        spawn.block = entity.as<QcPrimedExplosive>().block;
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
        spawn.name = entity.as<QcCreature>().name;
        spawn.appearance = creatureLook(entity.as<QcCreature>(), m_state.content());
        break;
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
    case QcEntityKind::Hanging: {
        // Its own item names its motifs (decision E-790); a frame shows the stack it holds.
        const auto &hanging = entity.as<QcHangingEntity>();
        spawn.hanging = hanging.hanging;
        spawn.item = hanging.held.forDisplay();
        spawn.held = m_state.content().itemId(hanging.item).value_or(qcNoItem);
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

void QcEntitySync::Pass::consider(const QcEntity &entity)
{
    // Items and orbs: small, many, seen from closer and sent again when their count or value changed.
    const bool orb = entity.kind == QcEntityKind::ExperienceOrb;
    const bool item = entity.kind == QcEntityKind::Item || orb;
    if (&entity == &client || entity.dormant || (item && !orb && entity.as<QcItemEntity>().surplus)) {
        return;
    }

    const QcVec3 position = entity.kind == QcEntityKind::MovingBlock
                                ? QcMovingBlocks::positionAt(entity.as<QcMovingBlock>(), sync.m_state.clock().tick())
                                : entity.motion.position;
    if ((position - client.motion.position).length() > (item ? itemViewDistance : entityViewDistance)) {
        return;
    }

    const QcMsg::EntityMove now = sync.seen(entity);

    visible.insert(entity.id);
    // Projectiles count whether they stick, so sticking and falling again send them anew.
    const bool projectile = entity.kind == QcEntityKind::Projectile;
    const int count = orb ? entity.as<QcExperienceOrb>().value
                      : item ? entity.as<QcItemEntity>().stack.count
                             : projectile && entity.as<QcProjectile>().stuck ? 1 : 0;
    const auto known = client.view.known.find(entity.id);
    if (known == client.view.known.end()) {
        client.view.known.insert(entity.id, {count, now.position});
        client.connection->send(sync.spawnOf(entity, now));
        return;
    }

    update(entity, now, {known, count});
}

void QcEntitySync::Pass::update(const QcEntity &entity, const QcMsg::EntityMove &now, const Known &known)
{
    const int count = known.count;
    const bool projectile = entity.kind == QcEntityKind::Projectile;

    switch (entity.kind) {
    case QcEntityKind::Item:
    case QcEntityKind::ExperienceOrb:
    case QcEntityKind::Projectile:
        // Sent again when the count (a projectile: whether it sticks) changed, moved only when the
        // position did.
        if (known.entry->count != count && projectile) {
            // Stuck or falling again: the new state, a move to where it is and a stop sample after it.
            *known.entry = {count, now.position, true};
            client.connection->send(sync.spawnOf(entity, now));
            moves.moves.append(now);
        } else if (known.entry->count != count) {
            *known.entry = {count, now.position, false};
            client.connection->send(sync.spawnOf(entity, now));
        } else if (const std::optional<QcServerClient::SentEntity> moved = QcEntitySync::changedMove(*known.entry, now, itemMoveThreshold)) {
            *known.entry = *moved;
            moves.moves.append(now);
        }
        break;
    case QcEntityKind::Minecart:
        // A cart standing still sends nothing.
        if (const std::optional<QcServerClient::SentEntity> moved = QcEntitySync::changedMove(*known.entry, now, cartMoveThreshold)) {
            *known.entry = *moved;
            moves.moves.append(now);
        }
        break;
    case QcEntityKind::ArmourStand:
    case QcEntityKind::Hanging:
        // Stands, frames and paintings send no moves: a spawn when they changed or were pushed.
        if (client.view.stale.remove(entity.id)) {
            client.connection->send(sync.spawnOf(entity, now));
        }
        break;
    default:
        // A known entity sent again (new look) still moves, swings and flashes in this tick.
        if (client.view.stale.remove(entity.id)) {
            client.connection->send(sync.spawnOf(entity, now));
        }
        moves.moves.append(now);
        break;
    }
}

std::optional<QcServerClient::SentEntity> QcEntitySync::changedMove(const QcServerClient::SentEntity &known, const QcMsg::EntityMove &now,
                                                                      double threshold)
{
    if ((known.position - now.position).length() > threshold) {
        return QcServerClient::SentEntity{known.count, now.position, true};
    }

    // Came to rest: one more move where it stays (a stop sample), so the client ends its motion
    // there instead of continuing the last step past the newest snapshot (QcSnapshotBuffer).
    if (known.moving) {
        return QcServerClient::SentEntity{known.count, now.position, false};
    }
    return std::nullopt;
}

void QcEntitySync::Pass::removeUnseen()
{
    QList<quint32> gone;
    for (auto it = client.view.known.begin(); it != client.view.known.end();) {
        if (!visible.contains(it.key())) {
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
        client.connection->send(QcMsg::EntityRemove{id, sync.m_state.entities().diedThisTick(id)});
    }
}
