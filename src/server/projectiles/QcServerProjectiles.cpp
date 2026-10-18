#include "server/projectiles/QcServerProjectiles.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/gameplay/QcServerInventory.h"

namespace {

bool isPresent(const QcServerClient &client)
{
    return client.state == QcServerClient::State::Playing && !client.dead;
}

} // namespace

QcServerProjectiles::QcServerProjectiles(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_health(health)
    , m_inventory(inventory)
    , m_launchers(state, sounds, inventory, *this)
    , m_throws(state, inventory, *this)
{
}

bool QcServerProjectiles::launch(const QcProjectileManager::Launch &shot)
{
    return m_state.entities().projectiles().launch(shot, m_state.entities().newId());
}

void QcServerProjectiles::tick()
{
    m_launchers.tick();
    const QList<QcProjectileManager::Launch> launches = m_state.entities().creatures().takeLaunches();
    for (const QcProjectileManager::Launch &shot : launches) {
        launch(shot);
    }

    QcProjectileManager &projectiles = m_state.entities().projectiles();
    if (projectiles.all().isEmpty()) {
        return;
    }

    const QcProjectileManager::Handlers handlers{
        [this](const QcProjectileManager::Hit &one) { return hit(one); },
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
        }};
    const QList<QcProjectileFlight::Body> bodies = projectiles.anyFlying() ? this->bodies() : QList<QcProjectileFlight::Body>();
    projectiles.tick(m_state.world(), bodies, collectors(), QcPhysics::tickDuration, handlers);
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
    const QcProjectile &projectile = hit.projectile;
    // Pushed along the flight, as if struck from behind.
    const QcVec3 along = QcVec3{projectile.motion.velocity.x, 0.0, projectile.motion.velocity.z}.normalized();
    const QcHitEffects::Impact impact = m_effects.impactOf(hit.definition, hit.damage);
    bool took = false;
    if (m_state.entities().creatures().find(hit.body)) {
        const bool byPlayer = std::ranges::any_of(m_state.clients(), [&](const auto &client) { return client->id == projectile.shooter; });
        took = QcServerCombat::hurtCreature(m_state, m_sounds,
                                            {hit.body, impact.damage, projectile.motion.position - along, byPlayer, impact.knockback});
        // The hit may have killed it.
        QcCreature *mob = took && impact.burnTicks > 0 ? m_state.entities().creatures().find(hit.body) : nullptr;
        if (mob) {
            mob->burnTicks = std::max(mob->burnTicks, impact.burnTicks);
        }
    } else {
        for (const auto &client : m_state.clients()) {
            if (client->id == hit.body && isPresent(*client) && client->hurt.wouldTake(impact.damage) != QcHurtWindow::Take::Ignored
                && !client->rules().invulnerable) {
                hitPlayer(*client, impact, along);
                took = true;
            }
        }
    }

    if (took && !hit.definition.hitSound.isEmpty()) {
        m_sounds.play(hit.definition.hitSound, projectile.motion.position);
    }
    return took;
}

void QcServerProjectiles::hitPlayer(QcServerClient &client, const QcHitEffects::Impact &impact, const QcVec3 &along)
{
    // Alight first, so the health sent with the damage says it burns.
    const bool alight = impact.burnTicks > client.burnTicks && client.burnTicks <= 0;
    client.burnTicks = std::max(client.burnTicks, impact.burnTicks);

    // The damage without its push, then the push of the knockback effects (also without damage).
    // A stronger hit during the invulnerability does not push (decision E-429).
    const QcHurtWindow::Take take = client.hurt.wouldTake(impact.damage);
    m_health.damage(client, impact.damage, {}, QcPlayerHealth::DamageKind::Armoured);
    if (client.dead) {
        client.burnTicks = 0;
        return;
    }

    if (take == QcHurtWindow::Take::Fresh) {
        QcPlayerHealth::knock(client, along, impact.knockback);
    }
    // A hit without damage sends no health by itself; the client still has to see the fire.
    if (alight && impact.damage <= 0.0) {
        m_health.sendHealth(client);
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
        if (!item || client->inventory.add(*item, 1, m_state.content(), 0) > 0) {
            return false;
        }
        m_sounds.play(QString::fromLatin1(QcSoundKeys::itemPickup), client->motion.position, 0.6f);
        m_inventory.send(*client);
        return true;
    }
    return false;
}
