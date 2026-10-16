#include "QcPlayerRecords.h"

#include "QcServerClient.h"
#include "QcServerGameModes.h"

#include <algorithm>

namespace QcPlayerRecords {

QcPlayerData of(const QcServerClient &client, const QcContentRegistry &content, int &lost)
{
    lost = 0;
    QcPlayerData player;
    player.motion = client.motion;
    player.yaw = client.yaw;
    player.pitch = client.pitch;
    player.inventory = client.inventory;

    // Items in the crafting grid are stored in the inventory, as if the grid had been closed; the
    // open grid itself is not saved.
    for (const QcItemStack &cell : client.crafting.grid) {
        if (!cell.isEmpty()) {
            lost += player.inventory.add(cell.item, cell.count, content, cell.damage);
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
    return player;
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
    QcServerGameModes::applyRules(client);
}

} // namespace QcPlayerRecords
