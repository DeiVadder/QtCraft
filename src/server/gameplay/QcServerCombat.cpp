#include "QcServerCombat.h"

#include "QcArmour.h"
#include "QcChatText.h"
#include "QcPhysics.h"
#include "QcServerInventory.h"
#include "QcServerSounds.h"
#include "QcServerState.h"
#include "QcSoundKeys.h"
#include "QcWorldConstants.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

QcServerCombat::QcServerCombat(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
{
}

void QcServerCombat::tick()
{
    const double dt = QcPhysics::tickDuration;
    QList<QcMobManager::PlayerView> players;
    for (const auto &client : m_state.clients) {
        if (client->state != QcServerClient::State::Playing) {
            continue;
        }

        tickPlayer(*client, dt);
        players.append({client->entityId, client->motion.position, !client->dead, !client->rules().invulnerable});
    }

    m_state.stands.tick(dt);
    const QList<QcMobManager::PlayerHit> hits =
        m_state.mobs.tick(m_state.world, players, m_state.info.worldTime, dt, [this] { return m_state.newEntityId(); });
    playMobSounds(dt);

    for (const QcMobManager::PlayerHit &hit : hits) {
        for (const auto &client : m_state.clients) {
            if (client->entityId == hit.playerEntityId) {
                damagePlayer(*client, hit.damage, hit.knockback, DamageKind::Armoured);
            }
        }
    }
}

void QcServerCombat::tickPlayer(QcServerClient &client, double dt)
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

    // Mining swings the arm again and again.
    if (client.miningPos && (m_state.tickCount - client.miningStartTick) % miningSwingTicks == 0) {
        client.swung = true;
    }
}

void QcServerCombat::applyVoidDamage(QcServerClient &client)
{
    if (client.dead || !QcWorld::isInVoid(client.motion.position.y)) {
        client.voidTicks = 0;
        return;
    }

    // The first hit comes at once, then one every interval for as long as the player stays down.
    if (client.voidTicks % voidDamageIntervalTicks == 0) {
        damagePlayer(client, voidDamage, {}, DamageKind::Void);
    }
    ++client.voidTicks;
}

void QcServerCombat::playMobSounds(double dt)
{
    for (const QcMobManager::Mob &mob : m_state.mobs.mobs()) {
        const QcMobDefinition &definition = m_state.content->mobs()[mob.type];
        if (!mob.dormant && !definition.ambientSound.isEmpty() && m_state.soundRandom.generateDouble() < dt / mobAmbientSeconds) {
            m_sounds.play(definition.ambientSound, mob.motion.position + QcVec3{0.0, definition.height * 0.8, 0.0});
        }
    }
}

namespace {

// Damage of the solid block under the centre of the feet (magma, decision E-209); a liquid next to
// the block a body stands on does not count.
double damageFromBelow(const QcServerState &state, const QcVec3 &feet)
{
    const std::optional<QcBlockId> below = state.world.blockAt(QcPhysics::blockContaining(feet - QcVec3{0.0, 0.01, 0.0}));
    if (!below || !state.content->isValidBlock(*below) || !state.content->block(*below).solid) {
        return 0.0;
    }
    return state.content->block(*below).contactDamage;
}

// Damage from what a body is inside of or stands on.
double contactDamageOf(const QcServerState &state, const QcVec3 &feet, double width, double height)
{
    return std::max(state.world.contactDamageIn(QcPhysics::boxAt(feet, width, height)), damageFromBelow(state, feet));
}

} // namespace

void QcServerCombat::applyContactDamage()
{
    if (!m_state.world.mayHaveContactDamage()) {
        return;
    }

    for (const auto &client : m_state.clients) {
        if (client->state != QcServerClient::State::Playing || client->dead) {
            continue;
        }

        const double damage = contactDamageOf(m_state, client->motion.position, QcPhysics::playerWidth, QcPhysics::playerHeight);
        if (damage > 0.0) {
            damagePlayer(*client, damage, {}, DamageKind::Armoured);
        }
    }

    // Hurting may remove a mob, so the damage is collected before any is dealt.
    QList<std::pair<quint32, double>> touched;
    for (const QcMobManager::Mob &mob : m_state.mobs.mobs()) {
        const QcMobDefinition &definition = m_state.content->mobs()[mob.type];
        if (const double damage = contactDamageOf(m_state, mob.motion.position, definition.width, definition.height);
            damage > 0.0) {
            touched.append({mob.id, damage});
        }
    }

    for (const auto &[id, damage] : std::as_const(touched)) {
        const QcMobManager::Mob *mob = m_state.mobs.find(id);
        if (!mob) {
            continue;
        }

        const QString sound = m_state.content->mobs()[mob->type].deathSound;
        const QcVec3 position = mob->motion.position;

        if (m_state.mobs.hurt(id, damage)) {
            m_sounds.play(sound, position);
            // Burnt creatures leave no loot.
            m_state.died.insert(id);
        }
    }

    m_state.items.removeWhere([this](const QcItemEntityManager::Item &item) {
        return m_state.world.contactDamageIn(QcPhysics::boxAt(item.motion.position, QcItemEntityManager::itemSize, QcItemEntityManager::itemSize)) > 0.0;
    });
}

void QcServerCombat::damagePlayer(QcServerClient &client, double amount, const QcVec3 &knockback, DamageKind kind)
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
        client.motion.velocity = knockback.normalized() * QcMobManager::knockbackSpeed + QcVec3{0.0, 5.0, 0.0};
        client.stateDirty = true;
    }

    if (client.health <= 0.0) {
        die(client, kind);
    }

    sendHealth(client);
}

void QcServerCombat::die(QcServerClient &client, DamageKind kind)
{
    client.dead = true;
    client.miningPos.reset();
    m_state.died.insert(client.entityId);

    const char *message = kind == DamageKind::Void ? QT_TRANSLATE_NOOP("QcServerMessages", "%1 fell out of the world")
                                                   : QT_TRANSLATE_NOOP("QcServerMessages", "%1 died");
    client.connection->send(QcChatText::message(message, {client.name}));

    // Catalogue items never fall into the world by themselves (decision E-283). Drops in the void
    // are removed at once by the item manager (decision E-288).
    if (!m_state.info.keepInventory && !QcGameModes::rules(client.gameMode).itemCatalogue) {
        m_inventory.dropAll(client);
    }
}

double QcServerCombat::throughArmour(QcServerClient &client, double amount)
{
    const QcContentRegistry &content = *m_state.content;
    const int points = QcArmour::points(content, client.equipment);
    if (points <= 0) {
        return amount;
    }

    QcEquipment worn = client.equipment;
    const bool broke = !QcArmour::wear(content, worn, amount).isEmpty();
    m_inventory.equipmentWorn(client, worn, broke);
    return QcArmour::reducedDamage(amount, points);
}

void QcServerCombat::updateHealth(QcServerClient &client, bool wasOnGround, bool wasInWater, double previousY)
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
            damagePlayer(client, std::floor(height - fallDamageThreshold), {});
        }
        client.fallPeak = client.motion.position.y;
    }
}

void QcServerCombat::attack(QcServerClient &client, quint32 entityId)
{
    if (client.dead || client.attackCooldown > 0.0) {
        return;
    }

    const QcMobManager::Mob *mob = m_state.mobs.find(entityId);
    if (!mob) {
        return;
    }

    const QcMobDefinition &definition = m_state.content->mobs()[mob->type];
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};

    // Generous slack for the mob's size and movement since the client aimed; no hits through walls.
    if ((centre - eye).length() > attackReach + definition.width + 1.0
        || !m_state.canSee(client, QcPhysics::boxAt(mob->motion.position, definition.width, definition.height))) {
        return;
    }

    client.attackCooldown = playerAttackCooldown;
    client.swung = true;
    client.hunger.exhaust(QcHunger::attack);

    const QcItemDefinition *held = m_state.heldItem(client);
    const double damage = held ? held->attackDamage : 1.0;
    const QcVec3 lootPosition = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};

    // Hits during the short invulnerability after a hit do nothing, and make no sound.
    const bool vulnerable = mob->hurtTimer <= 0.0;
    const std::optional<QList<QcItemDrop>> drops = m_state.mobs.damage(entityId, damage, client.motion.position);
    if (drops) {
        m_state.died.insert(entityId);
        m_sounds.play(definition.deathSound, lootPosition);
    } else if (vulnerable) {
        m_sounds.play(definition.hurtSound, lootPosition);
    }

    // Weapons wear like tools, except in creative mode (decision E-282).
    if (held && held->durability > 0 && !client.rules().infiniteItems) {
        const int slot = client.inventory.selectedHotbarSlot();
        QcItemStack tool = client.inventory.slot(slot);
        if (++tool.damage >= held->durability) {
            tool = {};
        }
        client.inventory.setSlot(slot, tool);
        m_inventory.send(client);
    }

    if (drops) {
        // Like block drops (decision E-028) the loot falls where the mob died.
        dropLoot(*drops, lootPosition);
    }
}

void QcServerCombat::dropLoot(const QList<QcItemDrop> &drops, const QcVec3 &position)
{
    for (const QcItemDrop &drop : drops) {
        if (drop.chance < 1.0 && m_state.random.generateDouble() >= drop.chance) {
            continue;
        }
        if (const std::optional<QcItemId> item = m_state.content->itemId(drop.item)) {
            m_state.items.spawnScattered({*item, drop.count, 0}, position, m_state.newEntityId());
        }
    }
}

void QcServerCombat::respawn(QcServerClient &client)
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
    client.motion.position = m_state.spawnPosition();
    client.previousPosition.reset();
    client.fallPeak = client.motion.position.y;
    client.stateDirty = true;

    sendHealth(client);
}

void QcServerCombat::consume(QcServerClient &client)
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
    if (!held->emptiesTo.isEmpty() && m_state.content->itemId(held->emptiesTo)) {
        m_inventory.exchangeHeld(client, held->emptiesTo);
    } else {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_sounds.play(QString::fromLatin1(QcSoundKeys::playerEat), client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0});
    sendFood(client);
    m_inventory.send(client);
}

void QcServerCombat::sendHealth(QcServerClient &client)
{
    client.connection->send(QcMsg::PlayerHealth{client.health, QcServerClient::maxHealth, client.dead});
}

void QcServerCombat::sendFood(QcServerClient &client)
{
    client.sentFood = client.hunger.food();
    client.connection->send(QcMsg::PlayerFood{client.hunger.food(), QcHunger::maxFood});
}
