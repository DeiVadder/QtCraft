// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/projectiles/QcServerProjectiles.h"

#include "common/content/registry/QcSoundKeys.h"
#include "common/gameplay/QcArmour.h"
#include "common/gameplay/QcEffectRules.h"
#include "common/physics/QcPhysics.h"
#include "server/animals/QcPetOwners.h"
#include "server/animals/QcServerAnimals.h"
#include "server/bosses/QcHydraRules.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcSafeSpot.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/gameplay/QcServerEffects.h"
#include "server/gameplay/QcServerInventory.h"

#include <algorithm>

namespace {

bool isPresent(const QcServerClient &client)
{
    return client.hasBody();
}

// The share of a splash at `at` with `radius` that reaches a body (decision E-624): 1 at the body,
// falling with the distance to its nearest point, 0 at the radius and beyond.
double splashShare(const QcVec3 &at, double radius, const QcAabb &box)
{
    const QcVec3 nearest{std::clamp(at.x, box.min.x, box.max.x), std::clamp(at.y, box.min.y, box.max.y), std::clamp(at.z, box.min.z, box.max.z)};
    return 1.0 - (nearest - at).length() / radius;
}

} // namespace

QcServerProjectiles::QcServerProjectiles(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health, QcServerInventory &inventory,
                                         QcServerExplosions &explosions)
    : m_state(state)
    , m_sounds(sounds)
    , m_health(health)
    , m_inventory(inventory)
    , m_explosions(explosions)
    , m_launchers(state, sounds, inventory, *this)
    , m_throws(state, inventory, *this)
    , m_fishing(state, sounds, inventory)
{
}

void QcServerProjectiles::useHeld(QcServerClient &client)
{
    const QcItemDefinition *held = m_state.heldItem(client);
    if (held && held->fishing) {
        m_fishing.use(client);
    } else {
        m_throws.throwHeld(client);
    }
}

bool QcServerProjectiles::launch(const QcProjectileManager::Launch &shot)
{
    return m_state.entities().projectiles().launch(shot, m_state.entities().newId());
}

void QcServerProjectiles::tick()
{
    m_launchers.tick();
    m_fishing.tick();
    const QList<QcProjectileManager::Launch> launches = m_state.entities().creatures().takeLaunches();
    for (const QcProjectileManager::Launch &shot : launches) {
        const QcProjectileDefinition *definition = m_state.content().projectile(shot.type);
        if (launch(shot) && definition && !definition->launchSound.isEmpty()) {
            m_sounds.play(definition->launchSound, shot.position);
        }
    }

    QcProjectileManager &projectiles = m_state.entities().projectiles();
    if (projectiles.all().isEmpty()) {
        return;
    }

    const QcProjectileManager::Handlers handlers{
        // A splash reaches the body hit with the others where the projectile breaks (decision E-624).
        [this](const QcProjectileManager::Hit &one) { return !one.definition.effect(QcHitEffect::splash) && hit(one); },
        [this](quint32 player, const QcProjectile &projectile) { return pickUp(player, projectile); },
        [this](const QcProjectile &projectile, const QcProjectileDefinition &definition) {
            if (!definition.stickSound.isEmpty()) {
                m_sounds.play(definition.stickSound, projectile.motion.position);
            }
        },
        [this](const QcProjectile &projectile, const QcProjectileDefinition &definition) {
            const QcHitEffect *effect = definition.effect(QcHitEffect::breaks);
            if (effect && !effect->sound.isEmpty()) {
                m_sounds.play(effect->sound, projectile.motion.position);
            }
            spawnWhereBroken(projectile, definition);
            if (const QcHitEffect *splash = definition.effect(QcHitEffect::splash)) {
                splashCreatures(projectile, definition, splash->number);
                splashPlayers(projectile, definition, splash->number);
            }
            if (const QcHitEffect *teleport = definition.effect(QcHitEffect::teleport)) {
                teleportThrower(projectile, *teleport);
            }
            if (const QcHitEffect *blast = definition.effect(QcHitEffect::explode)) {
                // Goes off after the pass: an explosion reaches the bodies the pass is walking past.
                // A hydra's skull spares its kind and soaks a hard block's resistance (E-1126).
                m_blasts.append({projectile.motion.position, blast->number,
                                 m_explosions.breaksBlocks() && (byPresentPlayer(projectile.shooter) || m_state.info().mobGriefing),
                                 definition.effect(QcHitEffect::ignite) != nullptr, definition.blastResistanceLimit,
                                 QcHydraRules::sparedBy(m_state.content(), m_state.entities().creatures(), projectile.shooter)});
            }
        },
        m_blockHit};
    const QList<QcProjectileFlight::Body> bodies = projectiles.anyFlying() ? this->bodies() : QList<QcProjectileFlight::Body>();
    projectiles.tick(m_state.world(), bodies, collectors(), QcPhysics::tickDuration, handlers);
    const QList<QcServerExplosions::Blast> blasts = std::exchange(m_blasts, {});
    for (const QcServerExplosions::Blast &blast : blasts) {
        m_explosions.explode(blast);
    }
}

bool QcServerProjectiles::byPresentPlayer(quint32 shooter) const
{
    return std::ranges::any_of(m_state.clients(), [&](const auto &client) { return client->id == shooter && isPresent(*client); });
}

bool QcServerProjectiles::mayKindle(quint32 shooter) const
{
    return m_state.info().mobGriefing || shooter == 0 || byPresentPlayer(shooter);
}

bool QcServerProjectiles::deflect(QcServerClient &client, quint32 id)
{
    QcProjectile *projectile = m_state.entities().projectiles().find(id);
    if (!projectile) {
        return false;
    }
    // Strikes that reach a flying projectile send it on; a strike at any other does nothing.
    const QcProjectileDefinition *definition = m_state.content().projectile(projectile->type);
    const QcVec3 eyes = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const std::optional<QcAabb> body = m_state.entities().boxOf(*projectile);
    if (!client.dead && definition && definition->deflectable && !projectile->stuck && (projectile->motion.position - eyes).length() <= deflectReach
        && body && QcLineOfSight::canSee(m_state.world(), client, *body)) {
        projectile->motion.velocity = QcProjectileFlight::direction(client.yaw, client.pitch) * projectile->motion.velocity.length();
        projectile->shooter = client.id;
        projectile->age = 0.0;
        QcProjectileFlight::turnAlong(*projectile);
        m_sounds.play(definition->hitSound, projectile->motion.position);
    }
    return true;
}

QList<QcProjectileFlight::Body> QcServerProjectiles::bodies() const
{
    const QcEntities &entities = m_state.entities();
    QList<QcProjectileFlight::Body> bodies;
    for (const QcCreature &mob : entities.creatures().mobs()) {
        if (const std::optional<QcAabb> box = mob.dormant ? std::nullopt : entities.boxOf(mob)) {
            bodies.append({mob.id, *box});
        }
    }
    for (const auto &client : m_state.clients()) {
        if (const std::optional<QcAabb> box = isPresent(*client) ? entities.boxOf(*client) : std::nullopt) {
            bodies.append({client->id, *box});
        }
    }
    return bodies;
}

QList<QcProjectileManager::PlayerView> QcServerProjectiles::collectors() const
{
    QList<QcProjectileManager::PlayerView> players;
    for (const auto &client : m_state.clients()) {
        if (const std::optional<QcAabb> box = isPresent(*client) ? m_state.entities().boxOf(*client) : std::nullopt) {
            players.append({client->id, *box});
        }
    }
    return players;
}

bool QcServerProjectiles::hit(const QcProjectileManager::Hit &hit)
{
    // Without hit effects (a fishing rod's bobber) nothing happens to the body.
    if (hit.definition.onHit.isEmpty()) {
        return false;
    }
    const QcProjectile &projectile = hit.projectile;
    // Pushed along the flight, as if struck from behind.
    const QcVec3 along = QcVec3{projectile.motion.velocity.x, 0.0, projectile.motion.velocity.z}.normalized();
    const QcHitEffects::Impact impact = m_effects.impactOf(hit.definition, hit.damage);
    bool took = false;
    if (m_state.entities().creatures().find(hit.body)) {
        took = hitCreature(hit, impact, along);
    } else {
        for (const auto &client : m_state.clients()) {
            // A shot whose harm is its fire is no hit on a fire-proof body (decision E-1058).
            if (client->id == hit.body && hit.definition.effect(QcHitEffect::kindle) && QcEffectRules::fireProof(m_state.content(), client->effects.all())) {
                return false;
            }
            if (client->id == hit.body && isPresent(*client) && client->hurt.wouldTake(impact.damage) != QcHurtWindow::Take::Ignored
                && !client->rules().invulnerable) {
                // A killing shot names the shooter when it is a creature (decision E-873).
                hitPlayer(*client, impact, along, noteShot(projectile.shooter, *client));
                took = true;
            }
        }
    }

    if (took && !hit.definition.hitSound.isEmpty()) {
        m_sounds.play(hit.definition.hitSound, projectile.motion.position);
    }
    return took;
}

bool QcServerProjectiles::hitCreature(const QcProjectileManager::Hit &hit, const QcHitEffects::Impact &impact, const QcVec3 &along)
{
    const QcProjectile &projectile = hit.projectile;
    const QcCreature *target = m_state.entities().creatures().find(hit.body);
    if (!target) {
        return false;
    }

    // A creature that dodges projectiles (a shadewalker, decision E-460) teleports away unhurt;
    // the projectile bounces off.
    const QcMobDefinition &definition = m_state.content().mobs()[target->type];
    // A player's arrow leaves the pets of other players unhurt (decision E-480).
    if (QcPetOwners::isOthersPet(m_state, *target, projectile.shooter)) {
        return false;
    }
    // A charred hydra takes no shot of the undead and, in its armour, none of arrows (E-1126).
    if (QcHydraRules::shields(m_state.content(), m_state.entities().creatures(), *target, hit.definition, projectile.shooter)) {
        return false;
    }
    if (definition.teleport && definition.teleport->dodgesProjectiles) {
        m_state.entities().creatures().teleport(hit.body, m_state.world());
        return false;
    }
    // A shot whose harm is its fire is no hit on a fire-proof body: no hurt, no anger, and a fire it
    // already has burns on as it did (decision E-1058).
    const bool fireProof = definition.fireImmune || (target->effects && QcEffectRules::fireProof(m_state.content(), target->effects->all()));
    if (fireProof && hit.definition.effect(QcHitEffect::kindle)) {
        return false;
    }
    const bool byPlayer = noteShot(projectile.shooter, hit.body);
    // Its own fire sent back by a player is what brings such a creature down (decision E-1053).
    const bool weak = byPlayer && definition.weakTo == hit.definition.name;
    const bool took = QcServerCombat::hurtCreature(m_state, m_sounds,
                                                        {.creature = hit.body,
                                                         .damage = weak ? definition.deflectedDamage : impact.damage,
                                                         .from = projectile.motion.position - along,
                                                         .attacker = byPlayer ? projectile.shooter : 0,
                                                         .knockback = impact.knockback});
    // The hit may have killed it.
    QcCreature *mob = took ? m_state.entities().creatures().find(hit.body) : nullptr;
    if (mob) {
        mob->burnTicks = std::max(mob->burnTicks, impact.burnTicks);
        for (QcStatusEffect effect : impact.effects) {
            effect.giver = byPlayer ? projectile.shooter : 0;
            QcServerEffects::giveCreature(m_state.content(), m_state.entities().creatures(), hit.body, effect);
        }
    }
    return took;
}

bool QcServerProjectiles::noteShot(quint32 shooter, quint32 creature)
{
    const auto player = std::ranges::find_if(m_state.clients(), [&](const auto &client) { return client->id == shooter; });
    if (player == m_state.clients().end()) {
        return false;
    }
    (*player)->foes.struck = creature;
    (*player)->foes.struckTick = m_state.clock().tick();
    return true;
}

quint32 QcServerProjectiles::noteShot(quint32 shooter, QcServerClient &player)
{
    if (!m_state.entities().creatures().find(shooter)) {
        return 0;
    }
    player.foes.hurtBy = shooter;
    player.foes.hurtTick = m_state.clock().tick();
    return shooter;
}

void QcServerProjectiles::hitPlayer(QcServerClient &client, const QcHitEffects::Impact &impact, const QcVec3 &along, quint32 creature)
{
    // Alight first, so the health sent with the damage says it burns.
    const bool alight = impact.burnTicks > client.burnTicks && client.burnTicks <= 0;
    client.burnTicks = std::max(client.burnTicks, impact.burnTicks);

    // The damage without its push, then the push of the knockback effects (also without damage).
    // A stronger hit during the invulnerability does not push (decision E-429).
    const QcHurtWindow::Take take = client.hurt.wouldTake(impact.damage);
    const QcHurtWindow::Take dealt = m_health.damage(client, impact.damage, {}, QcPlayerHealth::DamageKind::Shot, creature);
    if (client.dead) {
        client.burnTicks = 0;
        return;
    }

    if (take == QcHurtWindow::Take::Fresh) {
        // Worn armour holds back part of the push (decision E-1116).
        QcPlayerHealth::knock(client, along, impact.knockback * (1.0 - QcArmour::knockbackResistance(m_state.content(), client.equipment)));
    }
    // The status effects when the hit counts: it took, or, without damage, it would have taken.
    if ((impact.damage > 0.0 ? dealt : take) != QcHurtWindow::Take::Ignored) {
        for (const QcStatusEffect &effect : impact.effects) {
            QcServerEffects::givePlayer(m_state, client, effect);
        }
    }
    // A hit without damage sends no health by itself; the client still has to see the fire.
    if (alight && impact.damage <= 0.0) {
        m_health.sendHealth(client);
    }
}

void QcServerProjectiles::spawnWhereBroken(const QcProjectile &projectile, const QcProjectileDefinition &definition)
{
    QRandomGenerator &random = m_state.random();
    for (const QcHitEffect &effect : definition.onHit) {
        const std::optional<QcCreatureKindId> type = effect.is(QcHitEffect::spawn) ? m_state.content().mobKind(effect.subject) : std::nullopt;
        if (!type) {
            continue;
        }
        const int count = QcHitEffects::spawnCount(effect, random);
        for (int i = 0; i < count; ++i) {
            QcServerAnimals::spawnYoung(m_state, *type, projectile.motion.position);
        }
    }
}

void QcServerProjectiles::splashCreatures(const QcProjectile &projectile, const QcProjectileDefinition &definition, double radius)
{
    QcEntities &entities = m_state.entities();
    const QcVec3 at = projectile.motion.position;
    QList<std::pair<quint32, double>> creatures;
    for (const QcCreature &mob : entities.creatures().mobs()) {
        const std::optional<QcAabb> box = mob.dormant || mob.id == projectile.shooter ? std::nullopt : entities.boxOf(mob);
        if (const double share = box ? splashShare(at, radius, *box) : 0.0; share > 0.0) {
            creatures.append({mob.id, share});
        }
    }

    // Hurting may remove a creature from the list it was found in: deal after the search.
    for (const auto &[id, share] : std::as_const(creatures)) {
        const QcHitEffects::Impact impact = QcHitEffects::splashImpact(definition, share);
        const bool took = impact.damage <= 0.0
                          || QcServerCombat::hurtCreature(m_state, m_sounds, {.creature = id, .damage = impact.damage, .from = at, .knockback = 0.0});
        if (took && entities.creatures().find(id)) {
            for (const QcStatusEffect &effect : impact.effects) {
                QcServerEffects::giveCreature(m_state.content(), entities.creatures(), id, effect);
            }
        }
    }
}

void QcServerProjectiles::splashPlayers(const QcProjectile &projectile, const QcProjectileDefinition &definition, double radius)
{
    for (const auto &client : m_state.clients()) {
        const bool exposed = isPresent(*client) && client->id != projectile.shooter && !client->rules().invulnerable;
        const std::optional<QcAabb> box = exposed ? m_state.entities().boxOf(*client) : std::nullopt;
        const double share = box ? splashShare(projectile.motion.position, radius, *box) : 0.0;
        if (share <= 0.0) {
            continue;
        }

        // Like a status effect's damage: armour does not help (decision E-624).
        const QcHitEffects::Impact impact = QcHitEffects::splashImpact(definition, share);
        const QcHurtWindow::Take dealt = impact.damage > 0.0 ? m_health.damage(*client, impact.damage, {}, QcPlayerHealth::DamageKind::Effect)
                                                             : QcHurtWindow::Take::Fresh;
        if (client->dead || dealt == QcHurtWindow::Take::Ignored) {
            continue;
        }
        for (const QcStatusEffect &effect : impact.effects) {
            QcServerEffects::givePlayer(m_state, *client, effect);
        }
    }
}

void QcServerProjectiles::teleportThrower(const QcProjectile &projectile, const QcHitEffect &effect)
{
    // Only a player who is still there; the thrower is not saved, so a pearl that flew on after a
    // load teleports nobody.
    const auto it = std::ranges::find_if(m_state.clients(), [&](const auto &client) { return client->id == projectile.shooter; });
    if (it == m_state.clients().end() || !isPresent(**it)) {
        return;
    }

    // Where a player fits next to the place it broke (never into blocks, liquids or a chunk that
    // is not loaded); without one the pearl is lost.
    QcServerClient &client = **it;
    const std::optional<QcVec3> place = QcSafeSpot::nearest(m_state.world(), projectile.motion.position, projectile.motion.velocity,
                                                            QcSafeSpot::cellsOf(QcPhysics::playerHeight));
    if (!place) {
        return;
    }
    client.motion.position = *place;
    client.motion.velocity = {};
    client.previousPosition.reset();
    client.fallPeak = place->y;
    // Inputs from before the teleport no longer move with old effects (decision E-992).
    client.movementEpochs.forgetPast();
    client.mining.pos.reset();
    client.stateDirty = true;
    if (effect.number > 0.0) {
        m_health.damage(client, effect.number, {}, QcPlayerHealth::DamageKind::Unblockable);
    }
}

bool QcServerProjectiles::pickUp(quint32 player, const QcProjectile &projectile)
{
    for (const auto &client : m_state.clients()) {
        if (client->id != player) {
            continue;
        }

        // Shot in creative mode: no item was used, none comes back.
        if (projectile.pickup == QcProjectilePickup::Remove) {
            return true;
        }
        const QcProjectileDefinition *definition = m_state.content().projectile(projectile.type);
        const std::optional<QcItemId> item = definition ? m_state.content().itemId(definition->item) : std::nullopt;
        if (!item || client->inventory.add(*item, 1, m_state.content()) > 0) {
            return false;
        }
        m_sounds.play(QString::fromLatin1(QcSoundKeys::itemPickup), client->motion.position, 0.6f);
        m_inventory.send(*client);
        return true;
    }
    return false;
}
