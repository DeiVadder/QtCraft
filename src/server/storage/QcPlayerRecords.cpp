#include "server/storage/QcPlayerRecords.h"

#include "server/core/QcServerClient.h"
#include "server/gameplay/QcServerGameModes.h"

#include <algorithm>

namespace QcPlayerRecords {

Record of(const QcServerClient &client, const QcContentRegistry &content)
{
    Record record;
    QcPlayerData &player = record.player;
    player.motion = client.motion;
    player.yaw = client.yaw;
    player.pitch = client.pitch;
    player.inventory = client.inventory;

    // Items in the crafting grid and the stonecutter's input are stored in the inventory, as if the
    // screen had been closed; the open screen itself is not saved.
    QList<QcItemStack> held = client.crafting.grid;
    if (client.screen.stonecutter) {
        held.append(client.screen.stonecutter->input);
    }
    if (client.screen.enchanting) {
        held.append(client.screen.enchanting->item);
        held.append(client.screen.enchanting->lapis);
    }
    for (const QcItemStack &cell : std::as_const(held)) {
        if (!cell.isEmpty()) {
            record.lost += player.inventory.add(cell, content);
        }
    }

    player.health = client.health;
    player.food = client.hunger.food();
    player.exhaustion = client.hunger.exhaustion();
    player.equipment = client.equipment;
    player.gameMode = client.gameMode;
    player.keptInventory = client.keptInventory;
    player.keptEquipment = client.keptEquipment;
    player.experience = client.experience;
    player.enchantSeed = client.enchantSeed;
    player.effects = client.effects.all();
    return record;
}

void restore(QcServerClient &client, const QcPlayerData &player)
{
    client.motion = player.motion;
    client.fallPeak = client.motion.position.y;
    client.yaw = player.yaw;
    client.pitch = player.pitch;
    client.inventory = player.inventory;
    client.equipment = player.equipment;
    client.health = std::clamp(player.health, 0.0, QcServerClient::maxHealth);
    client.hunger.restore(player.food, player.exhaustion);
    client.gameMode = player.gameMode;
    client.keptInventory = player.keptInventory;
    client.keptEquipment = player.keptEquipment;
    client.experience = player.experience;
    client.enchantSeed = player.enchantSeed;
    client.effects.restore(player.effects);
    QcServerGameModes::applyRules(client);
}

} // namespace QcPlayerRecords
