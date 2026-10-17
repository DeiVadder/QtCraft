#include "server/gameplay/QcPlayerHealth.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "common/core/QcWorldConstants.h"
#include "common/gameplay/QcArmour.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/items/QcServerExperience.h"

#include <algorithm>
#include <cmath>

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
    client.hurtTimer = std::max(0.0, client.hurtTimer - dt);
    client.attackCooldown = std::max(0.0, client.attackCooldown - dt);

    if (!client.dead) {
        // Regeneration at high food, starvation at none (decision E-039).
        const double change = client.hunger.tick(dt, client.health, QcServerClient::maxHealth);
        if (change != 0.0) {
            client.health = std::clamp(client.health + change, 0.0, QcServerClient::maxHealth);
            client.hurtThisTick = client.hurtThisTick || change < 0.0;
            if (change < 0.0) {
                m_sounds.play(QString::fromLatin1(QcSoundKeys::playerHurt), client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0});
            }
            sendHealth(client);
        }
    }

    applyVoidDamage(client);
}

void QcPlayerHealth::applyVoidDamage(QcServerClient &client)
{
    if (client.dead || !QcWorld::isInVoid(client.motion.position.y)) {
        client.voidTicks = 0;
        return;
    }

    // The first hit comes at once, then one every interval for as long as the player stays down.
    if (client.voidTicks % voidDamageIntervalTicks == 0) {
        damage(client, voidDamage, {}, DamageKind::Void);
    }
    ++client.voidTicks;
}

void QcPlayerHealth::damage(QcServerClient &client, double amount, const QcVec3 &knockback, DamageKind kind)
{
    const bool fromVoid = kind == DamageKind::Void;
    if (client.dead || (client.hurtTimer > 0.0 && !fromVoid) || amount <= 0.0) {
        return;
    }
    if (client.rules().invulnerable && kind != DamageKind::Command && !fromVoid) {
        return;
    }

    if (kind == DamageKind::Armoured) {
        amount = throughArmour(client, amount);
    }
    client.health = std::max(0.0, client.health - amount);
    client.hurtTimer = QcMobManager::hurtInvulnerability;
    client.hurtThisTick = true;
    client.hunger.hurt();
    client.hunger.exhaust(QcHunger::damageTaken);

    m_sounds.play(QString::fromLatin1(client.health <= 0.0 ? QcSoundKeys::playerDeath : QcSoundKeys::playerHurt),
                  client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0});

    if (knockback.length() > 0.0) {
        knock(client, knockback);
    }

    if (client.health <= 0.0) {
        die(client, kind);
    }

    sendHealth(client);
}

void QcPlayerHealth::knock(QcServerClient &client, const QcVec3 &direction, double strength)
{
    if (direction.length() <= 0.0 || strength <= 0.0) {
        return;
    }
    client.motion.velocity = QcMobManager::knockbackVelocity(direction.normalized(), strength);
    client.stateDirty = true;
}

void QcPlayerHealth::die(QcServerClient &client, DamageKind kind)
{
    client.dead = true;
    client.mining.pos.reset();
    client.drawing = false;
    m_state.entities().noteDeath(client.id);

    const char *message = kind == DamageKind::Void ? QT_TRANSLATE_NOOP("QcServerMessages", "%1 fell out of the world")
                                                   : QT_TRANSLATE_NOOP("QcServerMessages", "%1 died");
    client.connection->send(QcChatText::message(message, {client.name}));

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

double QcPlayerHealth::throughArmour(QcServerClient &client, double amount)
{
    const QcContentRegistry &content = m_state.content();
    const int points = QcArmour::points(content, client.equipment);
    if (points <= 0) {
        return amount;
    }

    QcEquipment worn = client.equipment;
    const bool broke = !QcArmour::wear(content, worn, amount).isEmpty();
    m_inventory.equipmentWorn(client, worn, broke);
    return QcArmour::reducedDamage(amount, points);
}

void QcPlayerHealth::afterMove(QcServerClient &client, bool wasOnGround, bool wasInWater, double previousY)
{
    // Falls are measured from their highest point, including the position before the first
    // airborne tick; water, ladders, cobwebs and flying break a fall.
    if (client.motion.inWater || wasInWater || client.motion.climbing || client.motion.slowed || client.motion.flying) {
        client.fallPeak = client.motion.position.y;
    } else if (!client.motion.onGround) {
        client.fallPeak = wasOnGround ? std::max(previousY, client.motion.position.y)
                                      : std::max(client.fallPeak, client.motion.position.y);
    } else if (!wasOnGround) {
        const double height = client.fallPeak - client.motion.position.y;
        if (height > fallDamageThreshold) {
            damage(client, std::floor(height - fallDamageThreshold), {});
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

void QcPlayerHealth::sendHealth(QcServerClient &client)
{
    client.connection->send(QcMsg::PlayerHealth{client.health, QcServerClient::maxHealth, client.dead, client.burnTicks > 0});
}

void QcPlayerHealth::sendFood(QcServerClient &client)
{
    client.sentFood = client.hunger.food();
    client.connection->send(QcMsg::PlayerFood{client.hunger.food(), QcHunger::maxFood});
}
