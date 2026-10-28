#include "server/gameplay/QcPlayerHealth.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "common/core/QcWorldConstants.h"
#include "common/gameplay/QcArmour.h"
#include "common/gameplay/QcEnchanting.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/gameplay/QcToolWear.h"
#include "server/items/QcServerExperience.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// The key of the chat line that tells of a death by `kind` (QcServerMessages).
const char *deathMessage(QcPlayerHealth::DamageKind kind)
{
    switch (kind) {
    case QcPlayerHealth::DamageKind::Void:
        return QT_TRANSLATE_NOOP("QcServerMessages", "%1 fell out of the world");
    case QcPlayerHealth::DamageKind::Effect:
        return QT_TRANSLATE_NOOP("QcServerMessages", "%1 died of a status effect");
    case QcPlayerHealth::DamageKind::Unblockable:
    case QcPlayerHealth::DamageKind::Armoured:
    case QcPlayerHealth::DamageKind::Command:
    case QcPlayerHealth::DamageKind::Blow:
    case QcPlayerHealth::DamageKind::Shot:
        break;
    }
    return QT_TRANSLATE_NOOP("QcServerMessages", "%1 died");
}

} // namespace

QcPlayerHealth::QcPlayerHealth(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
{
}

void QcPlayerHealth::tick()
{
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing) {
            tickPlayer(*client, QcPhysics::tickDuration);
        }
    }
}

void QcPlayerHealth::tickPlayer(QcServerClient &client, double dt)
{
    // A lowered game rule shortens an open window at once (decision E-429).
    client.hurt.limit(m_state.info().playerInvulnerabilityTicks);
    client.hurt.tick();
    client.attackCooldown = std::max(0.0, client.attackCooldown - dt);

    if (!client.dead) {
        // Regeneration at high food, starvation at none (decision E-039). Starving is a hurt like
        // any other: it respects the invulnerability after a hurt and opens it, as in the reference
        // (decision E-429).
        const double change = client.hunger.tick(dt, client.health, QcServerClient::maxHealth);
        if (change > 0.0) {
            client.health = std::min(client.health + change, QcServerClient::maxHealth);
            sendHealth(client);
        } else if (change < 0.0) {
            damage(client, -change, {}, DamageKind::Unblockable);
        }
    }

    applyVoidDamage(client);
}

void QcPlayerHealth::applyVoidDamage(QcServerClient &client)
{
    // A spectator flies through the void unhurt (decision E-730).
    if (client.dead || !client.rules().tangible || !QcWorld::isInVoid(client.motion.position.y)) {
        client.voidTicks = 0;
        return;
    }

    // The first hit comes at once, then one every interval for as long as the player stays down.
    if (client.voidTicks % voidDamageIntervalTicks == 0) {
        damage(client, voidDamage, {}, DamageKind::Void);
    }
    ++client.voidTicks;
}

QcHurtWindow::Take QcPlayerHealth::damage(QcServerClient &client, double amount, const QcVec3 &knockback, DamageKind kind, quint32 creature)
{
    const bool fromVoid = kind == DamageKind::Void;
    const QcHurtWindow::Take take = fromVoid ? QcHurtWindow::Take::Fresh : client.hurt.wouldTake(amount);
    if (client.dead || take == QcHurtWindow::Take::Ignored || amount <= 0.0) {
        return QcHurtWindow::Take::Ignored;
    }
    if (client.rules().invulnerable && kind != DamageKind::Command && !fromVoid) {
        return QcHurtWindow::Take::Ignored;
    }
    // Nothing reaches a spectator, not even /kill: he would drop his survival inventory unseen
    // (decision E-730).
    if (!client.rules().tangible) {
        return QcHurtWindow::Take::Ignored;
    }

    // During the invulnerability a stronger hit deals the difference to the strongest hit of the
    // window (before armour, as in the reference), without push, hurt sound or flash (decision E-429).
    const int ticks = m_state.info().playerInvulnerabilityTicks;
    amount = fromVoid ? client.hurt.force(amount, ticks) : client.hurt.take(amount, ticks);
    if (kind == DamageKind::Armoured || kind == DamageKind::Blow || kind == DamageKind::Shot) {
        amount = throughArmour(client, amount);
    }
    client.health = std::max(0.0, client.health - amount);
    client.hunger.hurt();
    client.hunger.exhaust(QcHunger::damageTaken);

    const bool fresh = take == QcHurtWindow::Take::Fresh;
    client.hurtThisTick = client.hurtThisTick || fresh;
    if (fresh || client.health <= 0.0) {
        m_sounds.play(QString::fromLatin1(client.health <= 0.0 ? QcSoundKeys::playerDeath : QcSoundKeys::playerHurt),
                      client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0});
    }

    if (fresh && knockback.length() > 0.0) {
        knock(client, knockback);
    }

    if (client.health <= 0.0) {
        die(client, kind, creature);
    }

    sendHealth(client);
    return take;
}

void QcPlayerHealth::knock(QcServerClient &client, const QcVec3 &direction, double strength)
{
    if (direction.length() <= 0.0 || strength <= 0.0) {
        return;
    }
    client.motion.velocity = QcMobManager::knockbackVelocity(direction.normalized(), strength);
    client.stateDirty = true;
}

void QcPlayerHealth::die(QcServerClient &client, DamageKind kind, quint32 creature)
{
    client.dead = true;
    // A respawned player starts without a window from the last life (decision E-429).
    client.hurt.clear();
    client.mining.pos.reset();
    client.drawing = false;
    client.effects.clear();
    m_state.entities().noteDeath(client.id);
    forgetTeleports(client);

    client.connection->send(deathLine(client, kind, creature));

    // Catalogue items never fall into the world by themselves (decision E-283). Drops in the void
    // are removed at once by the item manager (decision E-288).
    if (!m_state.info().keepInventory && !QcGameModes::rules(client.gameMode).itemCatalogue) {
        m_inventory.dropAll(client);
        // Experience by the reference's rule: 7 points per level, at most 100, as orbs; the rest is
        // lost and the player comes back with none (decision E-398).
        QcServerExperience::drop(m_state, client.experience.deathDrop(), client.motion.position + QcVec3{0.0, 0.5, 0.0});
        client.experience = {};
    }
}

QcMsg::ChatMessage QcPlayerHealth::deathLine(const QcServerClient &client, DamageKind kind, quint32 creature) const
{
    // Only the creature of the killing hit itself: not the last that hurt the player
    // (QcServerClient::foes, which may lie before a dispenser's arrow in the same tick).
    const bool byCreature = (kind == DamageKind::Blow || kind == DamageKind::Shot) && creature != 0;
    const QcCreature *killer = byCreature ? m_state.entities().creatures().find(creature) : nullptr;
    if (!killer) {
        return QcChatText::message(deathMessage(kind), {client.name});
    }

    const char *key = kind == DamageKind::Shot ? QT_TRANSLATE_NOOP("QcServerMessages", "%1 was shot by %2")
                                               : QT_TRANSLATE_NOOP("QcServerMessages", "%1 was slain by %2");
    // A named creature by its name, as typed; another by its kind in the player's language.
    if (!killer->name.isEmpty()) {
        return QcChatText::message(key, {client.name, killer->name});
    }
    const QString kindKey = m_state.content().mobs()[killer->type].translationKey;
    return QcChatText::message(key, {client.name, m_state.content().translations().translate(kindKey, u"en"_s)}, {QString(), kindKey});
}

void QcPlayerHealth::forgetTeleports(const QcServerClient &client)
{
    for (QcProjectile &projectile : m_state.entities().projectiles().editableAll()) {
        const QcProjectileDefinition *definition = m_state.content().projectile(projectile.type);
        if (projectile.shooter == client.id && definition && definition->effect(QcHitEffect::teleport)) {
            projectile.shooter = 0;
        }
    }
}

double QcPlayerHealth::throughArmour(QcServerClient &client, double amount)
{
    const QcContentRegistry &content = m_state.content();
    const int points = QcArmour::points(content, client.equipment);
    if (points <= 0) {
        return amount;
    }

    // Protection enchantments take their share off what the armour lets through (decision E-864);
    // counted before the hit wears the armour, so a piece that breaks on this hit still protects.
    const int protection = QcEnchanting::protectionPoints(content, client.equipment, false);
    QcEquipment worn = client.equipment;
    const bool broke = !worn.wear(content, amount, [this](const QcItemStack &piece) { return QcToolWear::armourKeeps(m_state, piece); }).isEmpty();
    m_inventory.equipmentWorn(client, worn, broke);
    return QcEnchanting::reducedDamage(QcArmour::reducedDamage(amount, points), protection);
}

void QcPlayerHealth::afterMove(QcServerClient &client, bool wasOnGround, bool wasInWater, double previousY)
{
    // Falls are measured from their highest point, including the position before the first
    // airborne tick; water (also with only the feet in it, decision E-918), ladders, cobwebs and
    // flying break a fall.
    const bool wading = QcPhysics::feetInLiquid(m_state.world(), client.motion.position);
    if (client.motion.inWater || wasInWater || wading || client.motion.climbing || client.motion.slowed || client.motion.flying) {
        client.fallPeak = client.motion.position.y;
    } else if (!client.motion.onGround) {
        client.fallPeak = wasOnGround ? std::max(previousY, client.motion.position.y)
                                      : std::max(client.fallPeak, client.motion.position.y);
    } else if (!wasOnGround) {
        const double height = client.fallPeak - client.motion.position.y;
        if (height > fallDamageThreshold) {
            // Protection and feather falling soften a fall (decision E-864).
            const int protection = QcEnchanting::protectionPoints(m_state.content(), client.equipment, true);
            const double fall = QcEnchanting::reducedDamage(std::floor(height - fallDamageThreshold), protection);
            if (fall > 0.0) {
                damage(client, fall, {});
            }
        }
        client.fallPeak = client.motion.position.y;
    }
}

void QcPlayerHealth::respawn(QcServerClient &client)
{
    if (!client.dead) {
        return;
    }

    client.dead = false;
    client.health = QcServerClient::maxHealth;
    client.hurt.clear();
    // Frozen hunger keeps the survival values for the return to survival (decision E-282).
    if (!client.hunger.frozen()) {
        client.hunger.reset();
    }
    client.motion = {};
    client.motion.position = m_state.spawnPoint().position();
    client.previousPosition.reset();
    client.fallPeak = client.motion.position.y;
    client.stateDirty = true;

    sendHealth(client);
}

void QcPlayerHealth::consume(QcServerClient &client)
{
    const QcItemDefinition *held = m_state.heldItem(client);
    // Armour in the hand is put on (decision E-234).
    if (held && held->armour) {
        m_inventory.equipHeld(client);
        return;
    }

    if (held && held->drink) {
        drink(client, *held);
        return;
    }

    // Full players do not eat (decision E-039), nor do players with frozen hunger (creative mode,
    // decision E-282): the survival values stay as they are and the food is not used up.
    if (client.dead || client.hunger.frozen() || !held || held->food <= 0 || !client.hunger.eat(held->food)) {
        return;
    }

    // Soup leaves its bowl behind (decision E-212).
    if (!held->emptiesTo.isEmpty() && m_state.content().itemId(held->emptiesTo)) {
        m_inventory.exchangeHeld(client, held->emptiesTo);
    } else {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_sounds.play(QString::fromLatin1(QcSoundKeys::playerEat), client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0});
    sendFood(client);
    m_inventory.send(client);
}

void QcPlayerHealth::drink(QcServerClient &client, const QcItemDefinition &held)
{
    if (client.dead) {
        return;
    }

    // Milk ends every status effect (decision E-500); the client hears of it with its health.
    if (held.drink->clearsEffects && client.effects.clear()) { // NOLINT(bugprone-unchecked-optional-access): consume() checked it
        sendHealth(client);
    }
    // Creative players keep what they drink, like the items they place.
    if (!client.rules().infiniteItems) {
        if (!held.emptiesTo.isEmpty() && m_state.content().itemId(held.emptiesTo)) {
            m_inventory.exchangeHeld(client, held.emptiesTo);
        } else {
            client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
        }
    }
    m_sounds.play(QString::fromLatin1(QcSoundKeys::playerDrink), client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0});
    m_inventory.send(client);
}

void QcPlayerHealth::sendHealth(QcServerClient &client)
{
    client.sentEffects = client.effects;
    client.connection->send(QcMsg::PlayerHealth{client.health, QcServerClient::maxHealth, client.dead, client.burnTicks > 0, client.effects.all()});
}

void QcPlayerHealth::sendFood(QcServerClient &client)
{
    client.sentFood = client.hunger.food();
    client.connection->send(QcMsg::PlayerFood{client.hunger.food(), QcHunger::maxFood});
}
