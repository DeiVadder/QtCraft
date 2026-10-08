// Gameplay of QcServer: mining, placing, inventory, combat, health, entity sync and commands.

#include "QcServer.h"

#include "QcCrafting.h"
#include "QcDayCycle.h"
#include "QcMining.h"
#include "QcPhysics.h"
#include "QcProtocol.h"
#include "QcRaycast.h"
#include "QcServerClient.h"

#include <QLoggingCategory>
#include <QtMath>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

const QcItemDefinition *QcServer::heldItem(const Client &client) const
{
    const QcItemStack &held = client.inventory.selectedStack();
    return !held.isEmpty() && m_content->isValidItem(held.item) ? &m_content->item(held.item) : nullptr;
}

bool QcServer::isWithinReach(const Client &client, QcBlockPos pos) const
{
    // One metre of slack for movement between the client's decision and the server's check.
    constexpr double tolerance = 1.0;
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
    return (centre - eye).length() <= QcRaycast::playerReach + tolerance;
}

void QcServer::setBlockAndBroadcast(QcBlockPos pos, QcBlockId block)
{
    if (!m_world.setBlock(pos, block)) {
        return;
    }
    for (const auto &client : m_clients) {
        if (client->state == Client::State::Playing && client->sentChunks.contains(pos.chunk())) {
            client->connection->send(QcMsg::BlockUpdate{pos, block});
        }
    }
}

void QcServer::sendInventory(Client &client)
{
    QcMsg::InventoryState state;
    state.stacks = client.inventory.allSlots();
    state.selected = client.inventory.selectedHotbarSlot();
    state.craftingGrid = client.craftingGrid;
    state.craftingSize = client.craftingSize;
    if (const std::optional<qsizetype> recipe = craftingMatch(client)) {
        const QcRecipeDefinition &definition = m_content->recipes()[*recipe];
        if (const std::optional<QcItemId> item = m_content->itemId(definition.resultItem)) {
            state.craftingResult = {*item, definition.resultCount, 0};
        }
    }
    client.connection->send(state);
}

QList<QcItemStack> QcServer::craftingView(const Client &client) const
{
    QList<QcItemStack> view;
    for (int row = 0; row < client.craftingSize; ++row) {
        for (int column = 0; column < client.craftingSize; ++column) {
            view.append(client.craftingGrid[row * 3 + column]);
        }
    }
    return view;
}

std::optional<qsizetype> QcServer::craftingMatch(const Client &client) const
{
    return QcCrafting::match(*m_content, craftingView(client), client.craftingSize);
}

bool QcServer::isNearCraftingTable(const Client &client) const
{
    const QcBlockPos feet = QcPhysics::blockContaining(client.motion.position);
    const int reach = int(std::ceil(craftingTableReach));
    for (int dx = -reach; dx <= reach; ++dx) {
        for (int dy = -reach; dy <= reach; ++dy) {
            for (int dz = -reach; dz <= reach; ++dz) {
                const std::optional<QcBlockId> block = m_world.blockAt(feet.offset(dx, dy, dz));
                if (block && m_content->block(*block).opensCrafting
                    && QcVec3{double(dx), double(dy), double(dz)}.length() <= craftingTableReach) {
                    return true;
                }
            }
        }
    }
    return false;
}

void QcServer::handleMoveItem(Client &client, QcMsg::MoveInventoryItem move)
{
    // Inventory and the usable part of the grid form one slot space; other grid cells are invalid.
    const auto usable = [&client](int slot) {
        if (slot < craftingGridSlot) {
            return QcInventory::isValidSlot(slot);
        }
        const int cell = slot - craftingGridSlot;
        return cell >= 0 && cell < 9 && cell / 3 < client.craftingSize && cell % 3 < client.craftingSize;
    };
    if (client.dead || !usable(move.from) || !usable(move.to)) {
        sendInventory(client);
        return;
    }
    QList<QcItemStack> combined = client.inventory.allSlots();
    combined.append(client.craftingGrid);
    QcInventory::moveBetween(combined, move.from, move.to, std::max(0, move.count), *m_content);
    for (int i = 0; i < QcInventory::slotCount; ++i) {
        client.inventory.setSlot(i, combined[i]);
    }
    client.craftingGrid = combined.mid(QcInventory::slotCount);
    sendInventory(client);
}

void QcServer::handleTakeCraftingResult(Client &client)
{
    if (client.craftingSize == 3 && !isNearCraftingTable(client)) {
        closeCrafting(client);
        return;
    }
    const std::optional<qsizetype> recipe = craftingMatch(client);
    if (client.dead || !recipe) {
        sendInventory(client);
        return;
    }
    const QcRecipeDefinition &definition = m_content->recipes()[*recipe];
    const std::optional<QcItemId> item = m_content->itemId(definition.resultItem);
    QcInventory result = client.inventory;
    if (!item || result.add(*item, definition.resultCount, *m_content) > 0) {
        client.connection->send(QcMsg::ChatMessage{u"Not enough room in the inventory"_s});
        sendInventory(client);
        return;
    }
    client.inventory = result;
    for (int row = 0; row < client.craftingSize; ++row) {
        for (int column = 0; column < client.craftingSize; ++column) {
            QcItemStack &cell = client.craftingGrid[row * 3 + column];
            if (!cell.isEmpty() && --cell.count <= 0) {
                cell = {};
            }
        }
    }
    sendInventory(client);
}

void QcServer::handleCraftRecipe(Client &client, const QcMsg::CraftRecipe &request)
{
    const std::optional<qsizetype> index = m_content->recipeIndex(request.recipe);
    if (client.dead) {
        return;
    }
    if (!index) {
        client.connection->send(QcMsg::ChatMessage{u"Unknown recipe %1"_s.arg(request.recipe)});
        return;
    }
    const QcRecipeDefinition &recipe = m_content->recipes()[*index];
    if (QcCrafting::gridSizeNeeded(recipe) > 2 && !isNearCraftingTable(client)) {
        client.connection->send(QcMsg::ChatMessage{u"%1 needs a crafting table nearby"_s.arg(recipe.name)});
        return;
    }
    const std::optional<QcItemId> result = m_content->itemId(recipe.resultItem);
    const QList<std::pair<QString, int>> needed = QcCrafting::requirements(recipe);
    int crafted = 0;
    for (int round = 0; round < std::clamp(request.count, 1, 64) && result; ++round) {
        // All or nothing per round, on a copy so a full inventory leaves everything unchanged.
        QcInventory attempt = client.inventory;
        bool ok = true;
        for (const auto &[name, count] : needed) {
            const std::optional<QcItemId> ingredient = m_content->itemId(name);
            ok = ok && ingredient && attempt.removeItems(*ingredient, count);
        }
        if (!ok || attempt.add(*result, recipe.resultCount, *m_content) > 0) {
            break;
        }
        client.inventory = attempt;
        ++crafted;
    }
    client.connection->send(QcMsg::ChatMessage{crafted > 0 ? u"Crafted %1 x %2"_s.arg(crafted * recipe.resultCount).arg(recipe.resultItem)
                                                           : u"Missing ingredients or room for %1"_s.arg(recipe.name)});
    sendInventory(client);
}

void QcServer::closeCrafting(Client &client)
{
    // Items left in the grid go back into the inventory; what does not fit is lost (E-019).
    int lost = 0;
    for (QcItemStack &cell : client.craftingGrid) {
        if (!cell.isEmpty()) {
            lost += client.inventory.add(cell.item, cell.count, *m_content);
            cell = {};
        }
    }
    if (lost > 0) {
        client.connection->send(QcMsg::ChatMessage{u"Inventory full: %1 items from the crafting grid were lost"_s.arg(lost)});
    }
    client.craftingSize = 2;
    client.craftingTable.reset();
    sendInventory(client);
}

void QcServer::handleConsume(Client &client)
{
    const QcItemDefinition *held = heldItem(client);
    if (client.dead || !held || held->foodHealth <= 0.0 || client.health >= playerMaxHealth) {
        return;
    }
    client.health = std::min(playerMaxHealth, client.health + held->foodHealth);
    client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    sendHealth(client);
    sendInventory(client);
}

void QcServer::handleFinishMining(Client &client, QcBlockPos pos)
{
    const bool sameTarget = client.miningPos && *client.miningPos == pos;
    client.miningPos.reset();
    const std::optional<QcBlockId> block = m_world.blockAt(pos);
    if (!sameTarget || !block || *block == qcAirBlockId || !isWithinReach(client, pos)) {
        // Resend the true block so a client that predicted the break shows it again.
        if (block) {
            client.connection->send(QcMsg::BlockUpdate{pos, *block});
        }
        return;
    }
    const QcBlockDefinition &definition = m_content->block(*block);
    const QcItemDefinition *held = heldItem(client);
    const double seconds = QcMining::breakSeconds(definition, held);
    const auto requiredTicks = quint64(std::ceil(seconds * QcPhysics::tickRate));
    if (seconds < 0.0 || m_tickCount - client.miningStartTick + miningToleranceTicks < requiredTicks) {
        client.connection->send(QcMsg::BlockUpdate{pos, *block});
        return;
    }
    setBlockAndBroadcast(pos, qcAirBlockId);
    // Drops go straight into the inventory; what does not fit is lost.
    if (QcMining::canHarvest(definition, held)) {
        for (const QcItemDrop &drop : definition.drops) {
            if (const std::optional<QcItemId> item = m_content->itemId(drop.item)) {
                client.inventory.add(*item, drop.count, *m_content);
            }
        }
    }
    if (held && held->durability > 0) {
        const int slot = client.inventory.selectedHotbarSlot();
        QcItemStack tool = client.inventory.slot(slot);
        if (++tool.damage >= held->durability) {
            tool = {};
        }
        client.inventory.setSlot(slot, tool);
    }
    sendInventory(client);
}

void QcServer::handleUseItem(Client &client, QcMsg::UseItem message)
{
    // Using a crafting table opens its 3 x 3 grid unless the player sneaks to place against it.
    if (const std::optional<QcBlockId> clickedBlock = m_world.blockAt(message.target);
        clickedBlock && m_content->block(*clickedBlock).opensCrafting && !client.sneaking && isWithinReach(client, message.target)) {
        client.craftingSize = 3;
        client.craftingTable = message.target;
        client.connection->send(QcMsg::OpenCrafting{3});
        sendInventory(client);
        return;
    }
    const QcBlockPos target = qcNeighbour(message.target, message.face);
    const QcItemDefinition *held = heldItem(client);
    const std::optional<QcBlockId> clicked = m_world.blockAt(message.target);
    const std::optional<QcBlockId> existing = m_world.blockAt(target);
    const std::optional<QcBlockId> placed = held ? m_content->blockId(held->placesBlock) : std::nullopt;
    const bool clickedSolid = clicked && m_content->isValidBlock(*clicked) && !m_content->block(*clicked).liquid;
    const auto reject = [&] {
        if (existing) {
            client.connection->send(QcMsg::BlockUpdate{target, *existing});
        }
        sendInventory(client);
    };
    if (!placed || !clickedSolid || *clicked == qcAirBlockId || !existing || !target.isInsideWorldHeight()
        || !m_content->block(*existing).replaceable || !isWithinReach(client, target)) {
        reject();
        return;
    }
    if (m_content->block(*placed).solid) {
        const QcAabb blockBox{{double(target.x), double(target.y), double(target.z)},
                              {target.x + 1.0, target.y + 1.0, target.z + 1.0}};
        for (const auto &other : m_clients) {
            if (other->state == Client::State::Playing
                && QcPhysics::boxAt(other->motion.position, QcPhysics::playerWidth, QcPhysics::playerHeight).intersects(blockBox)) {
                reject();
                return;
            }
        }
    }
    setBlockAndBroadcast(target, *placed);
    client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    sendInventory(client);
}

void QcServer::handleChat(Client &client, const QString &text)
{
    const QStringList words = text.split(u' ', Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        return;
    }
    const auto reply = [&client](const QString &line) { client.connection->send(QcMsg::ChatMessage{line}); };
    if (!words.first().startsWith(u'/')) {
        for (const auto &other : m_clients) {
            if (other->state == Client::State::Playing) {
                other->connection->send(QcMsg::ChatMessage{u"<%1> %2"_s.arg(client.name, text)});
            }
        }
        return;
    }
    const QString command = words.first().mid(1).toLower();
    if (command == u"help"_s) {
        reply(u"Commands: /help, /give <item> [count], /save, /time set <day|night|seconds>, /time add <seconds>, "
              u"/summon <mob> [count], /kill, /heal"_s);
    } else if (!m_config.allowCheats
               && (command == u"give"_s || command == u"time"_s || command == u"summon"_s || command == u"heal"_s
                   || command == u"kill"_s)) {
        reply(u"Cheats are disabled on this server"_s);
    } else if (command == u"time"_s && words.size() >= 3) {
        bool ok = true;
        double seconds = 0.0;
        const double dayStart = std::floor(m_info.worldTime / QcDayCycle::dayLength) * QcDayCycle::dayLength;
        if (words[1] == u"set"_s && words[2] == u"day"_s) {
            seconds = dayStart + QcDayCycle::dayLength * 1.05;
        } else if (words[1] == u"set"_s && words[2] == u"night"_s) {
            seconds = dayStart + QcDayCycle::dayLength * 0.6;
        } else if (words[1] == u"set"_s) {
            seconds = words[2].toDouble(&ok);
        } else if (words[1] == u"add"_s) {
            seconds = m_info.worldTime + words[2].toDouble(&ok);
        } else {
            ok = false;
        }
        if (!ok || !std::isfinite(seconds) || seconds < 0.0 || seconds > 1e9) {
            reply(u"Usage: /time set <day|night|seconds> or /time add <seconds>"_s);
            return;
        }
        setWorldTime(seconds);
        reply(u"Time is now %1 s (%2)"_s.arg(qRound(seconds)).arg(QcDayCycle::isNight(seconds) ? u"night"_s : u"day"_s));
    } else if (command == u"summon"_s && words.size() >= 2) {
        const QString name = words[1].contains(u':') ? words[1] : u"qtcraft:"_s + words[1];
        const std::optional<int> type = m_content->mobIndex(name);
        bool countOk = true;
        const int count = words.size() >= 3 ? words[2].toInt(&countOk) : 1;
        if (!type || !countOk || count < 1 || count > 16) {
            reply(u"Unknown mob or invalid count: %1"_s.arg(text));
            return;
        }
        if (m_mobs.mobs().size() + count > QcMobManager::summonLimit) {
            reply(u"Too many creatures in the world (at most %1)"_s.arg(QcMobManager::summonLimit));
            return;
        }
        // Three metres in front of the player.
        const double yaw = qDegreesToRadians(double(client.yaw));
        const QcVec3 position = client.motion.position + QcVec3{std::sin(yaw) * 3.0, 0.5, -std::cos(yaw) * 3.0};
        for (int i = 0; i < count; ++i) {
            const int row = i / 3; // NOLINT(bugprone-integer-division): grid row
            m_mobs.spawn(*type, position + QcVec3{(i % 3) * 0.8, 0.0, row * 0.8}, m_nextEntityId++);
        }
        reply(u"Summoned %1 x %2"_s.arg(count).arg(name));
    } else if (command == u"kill"_s) {
        client.hurtTimer = 0.0;
        damagePlayer(client, client.health, {});
    } else if (command == u"heal"_s && client.dead) {
        reply(u"Respawn first"_s);
    } else if (command == u"heal"_s) {
        client.health = playerMaxHealth;
        sendHealth(client);
        reply(u"Healed"_s);
    } else if (command == u"save"_s) {
        const QcStatus saved = save();
        reply(saved ? u"World saved"_s : u"Saving failed: %1"_s.arg(saved.error()));
    } else if (command == u"give"_s && words.size() >= 2) {
        const QString name = words[1].contains(u':') ? words[1] : u"qtcraft:"_s + words[1];
        const std::optional<QcItemId> item = m_content->itemId(name);
        bool countOk = true;
        const int count = words.size() >= 3 ? words[2].toInt(&countOk) : 1;
        if (!item || !countOk || count < 1 || count > 64 * QcInventory::slotCount) {
            reply(u"Unknown item or invalid count: %1"_s.arg(text));
            return;
        }
        const int leftover = client.inventory.add(*item, count, *m_content);
        sendInventory(client);
        reply(u"Gave %1 x %2"_s.arg(count - leftover).arg(name));
    } else {
        reply(u"Unknown command: %1"_s.arg(words.first()));
    }
}

void QcServer::setWorldTime(double seconds)
{
    m_info.worldTime = std::max(0.0, seconds);
    for (const auto &client : m_clients) {
        if (client->state == Client::State::Playing) {
            sendTime(*client);
        }
    }
}

void QcServer::sendHealth(Client &client)
{
    client.connection->send(QcMsg::PlayerHealth{client.health, playerMaxHealth, client.dead});
}

void QcServer::sendTime(Client &client)
{
    client.connection->send(QcMsg::WorldTime{m_info.worldTime, QcDayCycle::dayLength});
}

void QcServer::damagePlayer(Client &client, double amount, const QcVec3 &knockback)
{
    if (client.dead || client.hurtTimer > 0.0 || amount <= 0.0) {
        return;
    }
    client.health = std::max(0.0, client.health - amount);
    client.hurtTimer = QcMobManager::hurtInvulnerability;
    client.regenerationTimer = 0.0;
    if (knockback.length() > 0.0) {
        client.motion.velocity = knockback.normalized() * QcMobManager::knockbackSpeed + QcVec3{0.0, 5.0, 0.0};
        client.stateDirty = true;
    }
    if (client.health <= 0.0) {
        client.dead = true;
        client.miningPos.reset();
        client.connection->send(QcMsg::ChatMessage{u"%1 died"_s.arg(client.name)});
    }
    sendHealth(client);
}

void QcServer::updateHealth(Client &client, bool wasOnGround, bool wasInWater, double previousY)
{
    // Falls are measured from their highest point, including the position before the first
    // airborne tick; water breaks a fall.
    if (client.motion.inWater || wasInWater) {
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

void QcServer::handleAttack(Client &client, quint32 entityId)
{
    if (client.dead || client.attackCooldown > 0.0) {
        return;
    }
    const QcMobManager::Mob *mob = m_mobs.find(entityId);
    if (!mob) {
        return;
    }
    const QcMobDefinition &definition = m_content->mobs()[mob->type];
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    // Generous slack for the mob's size and movement since the client aimed.
    if ((centre - eye).length() > attackReach + definition.width + 1.0) {
        return;
    }
    client.attackCooldown = playerAttackCooldown;
    const QcItemDefinition *held = heldItem(client);
    const double damage = held ? held->attackDamage : 1.0;
    const std::optional<QList<QcItemDrop>> drops = m_mobs.damage(entityId, damage, client.motion.position);
    if (held && held->durability > 0) {
        const int slot = client.inventory.selectedHotbarSlot();
        QcItemStack tool = client.inventory.slot(slot);
        if (++tool.damage >= held->durability) {
            tool = {};
        }
        client.inventory.setSlot(slot, tool);
        sendInventory(client);
    }
    if (drops) {
        // Like block drops (decision E-019) the loot goes straight into the killer's inventory.
        for (const QcItemDrop &drop : *drops) {
            if (const std::optional<QcItemId> item = m_content->itemId(drop.item)) {
                client.inventory.add(*item, drop.count, *m_content);
            }
        }
        sendInventory(client);
    }
}

void QcServer::handleRespawn(Client &client)
{
    if (!client.dead) {
        return;
    }
    client.dead = false;
    client.health = playerMaxHealth;
    client.motion = {};
    client.motion.position = spawnPosition();
    client.fallPeak = client.motion.position.y;
    client.stateDirty = true;
    sendHealth(client);
}

void QcServer::tickGameplay()
{
    const double dt = QcPhysics::tickDuration;
    QList<QcMobManager::PlayerView> players;
    for (const auto &client : m_clients) {
        if (client->state != Client::State::Playing) {
            continue;
        }
        client->hurtTimer = std::max(0.0, client->hurtTimer - dt);
        client->attackCooldown = std::max(0.0, client->attackCooldown - dt);
        if (!client->dead && client->health < playerMaxHealth) {
            client->regenerationTimer += dt;
            if (client->regenerationTimer >= regenerationInterval) {
                client->regenerationTimer = 0.0;
                client->health = std::min(playerMaxHealth, client->health + 1.0);
                sendHealth(*client);
            }
        }
        players.append({client->entityId, client->motion.position, !client->dead});
    }
    const QList<QcMobManager::PlayerHit> hits =
        m_mobs.tick(m_world, players, m_info.worldTime, dt, [this] { return m_nextEntityId++; });
    for (const QcMobManager::PlayerHit &hit : hits) {
        for (const auto &client : m_clients) {
            if (client->entityId == hit.playerEntityId) {
                damagePlayer(*client, hit.damage, hit.knockback);
            }
        }
    }
}

void QcServer::syncEntities(Client &client)
{
    QSet<quint32> visible;
    QcMsg::EntityMoves moves;
    const auto consider = [&](quint32 id, qint32 type, const QString &name, const QcVec3 &position, float yaw, double health,
                              bool hurt) {
        if ((position - client.motion.position).length() > entityViewDistance) {
            return;
        }
        visible.insert(id);
        if (!client.knownEntities.contains(id)) {
            client.connection->send(QcMsg::EntitySpawn{id, type, name, position, yaw, health});
            client.knownEntities.insert(id);
        } else {
            moves.moves.append({id, position, yaw, health, hurt});
        }
    };
    for (const QcMobManager::Mob &mob : m_mobs.mobs()) {
        consider(mob.id, mob.type, QString(), mob.motion.position, mob.yaw, mob.health, mob.hurtThisTick);
    }
    for (const auto &other : m_clients) {
        if (other.get() != &client && other->state == Client::State::Playing && !other->dead) {
            consider(other->entityId, -1, other->name, other->motion.position, other->yaw, other->health, false);
        }
    }
    for (auto it = client.knownEntities.begin(); it != client.knownEntities.end();) {
        if (!visible.contains(*it)) {
            client.connection->send(QcMsg::EntityRemove{*it});
            it = client.knownEntities.erase(it);
        } else {
            ++it;
        }
    }
    if (!moves.moves.isEmpty()) {
        client.connection->send(moves);
    }
}
