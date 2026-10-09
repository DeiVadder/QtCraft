#include "QcServerInventory.h"

#include "QcCrafting.h"
#include "QcPhysics.h"
#include "QcServerMachines.h"
#include "QcServerState.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Inventory, the usable part of the grid and the open machine or chest form one slot space;
// other grid cells and block slots without an open block are invalid.
bool isUsableSlot(const QcServerClient &client, int slot, const QcMachineManager::Machine *machine,
                  const QcContainerManager::Container *container)
{
    if (slot < QcServerInventory::craftingGridSlot) {
        return QcInventory::isValidSlot(slot);
    }

    if (slot >= QcServerInventory::machineSlot) {
        return (machine && slot < QcServerInventory::machineSlot + QcMachineManager::slotCount)
               || (container && slot < QcServerInventory::machineSlot + container->stacks.size());
    }

    const int cell = slot - QcServerInventory::craftingGridSlot;
    return cell >= 0 && cell < 9 && cell / 3 < client.craftingSize && cell % 3 < client.craftingSize;
}

} // namespace

QcServerInventory::QcServerInventory(QcServerState &state, QcServerMachines &machines)
    : m_state(state)
    , m_machines(machines)
{
}

void QcServerInventory::send(QcServerClient &client)
{
    QcMsg::InventoryState state;
    state.stacks = client.inventory.allSlots();
    state.selected = client.inventory.selectedHotbarSlot();
    state.craftingGrid = client.craftingGrid;
    state.craftingSize = client.craftingSize;

    if (const std::optional<qsizetype> recipe = craftingMatch(client)) {
        const QcRecipeDefinition &definition = m_state.content->recipes()[*recipe];
        if (const std::optional<QcItemId> item = m_state.content->itemId(definition.resultItem)) {
            state.craftingResult = {*item, definition.resultCount, 0};
        }
    }

    client.connection->send(state);
}

QList<QcItemStack> QcServerInventory::craftingView(const QcServerClient &client) const
{
    QList<QcItemStack> view;
    for (int row = 0; row < client.craftingSize; ++row) {
        for (int column = 0; column < client.craftingSize; ++column) {
            view.append(client.craftingGrid[row * 3 + column]);
        }
    }
    return view;
}

std::optional<qsizetype> QcServerInventory::craftingMatch(const QcServerClient &client) const
{
    return QcCrafting::match(*m_state.content, craftingView(client), client.craftingSize);
}

bool QcServerInventory::isNearCraftingTable(const QcServerClient &client) const
{
    const QcBlockPos feet = QcPhysics::blockContaining(client.motion.position);
    const int reach = int(std::ceil(craftingTableReach));

    for (int dx = -reach; dx <= reach; ++dx) {
        for (int dy = -reach; dy <= reach; ++dy) {
            if (hasCraftingTableInColumn(feet, dx, dy)) {
                return true;
            }
        }
    }
    return false;
}

bool QcServerInventory::hasCraftingTableInColumn(QcBlockPos feet, int dx, int dy) const
{
    const int reach = int(std::ceil(craftingTableReach));
    for (int dz = -reach; dz <= reach; ++dz) {
        const std::optional<QcBlockId> block = m_state.world.blockAt(feet.offset(dx, dy, dz));
        if (block && m_state.content->block(*block).opensCrafting
            && QcVec3{double(dx), double(dy), double(dz)}.length() <= craftingTableReach) {
            return true;
        }
    }
    return false;
}

void QcServerInventory::moveItem(QcServerClient &client, QcMsg::MoveInventoryItem move)
{
    QcMachineManager::Machine *machine = m_machines.openMachineOf(client);
    QcContainerManager::Container *container = m_machines.openContainerOf(client);
    const auto refuse = [&] {
        send(client);
        m_machines.send(client, true);
    };

    if (client.dead || !isUsableSlot(client, move.from, machine, container) || !isUsableSlot(client, move.to, machine, container)
        || (machine && move.to == QcMsg::MoveInventoryItem::machineOutputSlot)) {
        refuse();
        return;
    }

    QList<QcItemStack> combined = client.inventory.allSlots();
    combined.append(client.craftingGrid);
    if (machine) {
        combined.append(QList<QcItemStack>(machine->stacks.begin(), machine->stacks.end()));
    } else if (container) {
        combined.append(container->stacks);
    }

    QcInventory::moveBetween(combined, move.from, move.to, std::max(0, move.count), *m_state.content);

    if (machine) {
        if (!machineAccepts(*machine, combined)) {
            refuse();
            return;
        }
        for (int slot = 0; slot < QcMachineManager::slotCount; ++slot) {
            machine->stacks[std::size_t(slot)] = combined[machineSlot + slot];
        }
    }

    for (int i = 0; i < QcInventory::slotCount; ++i) {
        client.inventory.setSlot(i, combined[i]);
    }
    client.craftingGrid = combined.mid(QcInventory::slotCount, 9);
    if (container) {
        container->stacks = combined.mid(machineSlot, container->stacks.size());
    }

    send(client);
    if (container) {
        m_machines.sendToViewers(container->pos);
    }
    if (machine) {
        // Everyone looking into this machine sees the change.
        m_machines.sendToViewers(machine->pos);
    }
}

bool QcServerInventory::machineAccepts(const QcMachineManager::Machine &machine, const QList<QcItemStack> &combined) const
{
    // Only fuel goes into the fuel slot, and nothing goes into the output (not even by a swap).
    // An emptied container left behind by burnt fuel may stay or be taken out.
    const QcItemStack &fuel = combined[machineSlot + QcMachineManager::fuelSlot];
    const QcItemStack &previousFuel = machine.stacks[QcMachineManager::fuelSlot];
    const QcItemStack &output = combined[machineSlot + QcMachineManager::outputSlot];
    const QcItemStack &previousOutput = machine.stacks[QcMachineManager::outputSlot];

    const bool fuelOk = fuel.isEmpty() || m_state.machines.fuelSeconds(fuel.item) > 0.0
                        || (fuel.item == previousFuel.item && fuel.damage == previousFuel.damage && fuel.count <= previousFuel.count);
    const bool outputOk = output.isEmpty() || (output.item == previousOutput.item && output.count <= previousOutput.count);
    return fuelOk && outputOk;
}

void QcServerInventory::takeCraftingResult(QcServerClient &client)
{
    if (client.craftingSize == 3 && !isNearCraftingTable(client)) {
        closeCrafting(client);
        return;
    }

    const std::optional<qsizetype> recipe = craftingMatch(client);
    if (client.dead || !recipe) {
        send(client);
        return;
    }

    const QcContentRegistry &content = *m_state.content;
    const QcRecipeDefinition &definition = content.recipes()[*recipe];
    const std::optional<QcItemId> item = content.itemId(definition.resultItem);
    QcInventory result = client.inventory;
    if (!item || result.add(*item, definition.resultCount, content) > 0) {
        client.connection->send(QcMsg::ChatMessage{u"Not enough room in the inventory"_s});
        send(client);
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

    send(client);
}

void QcServerInventory::craftRecipe(QcServerClient &client, const QcMsg::CraftRecipe &request)
{
    const QcContentRegistry &content = *m_state.content;
    const std::optional<qsizetype> index = content.recipeIndex(request.recipe);
    if (client.dead) {
        return;
    }

    if (!index) {
        client.connection->send(QcMsg::ChatMessage{u"Unknown recipe %1"_s.arg(request.recipe)});
        return;
    }

    const QcRecipeDefinition &recipe = content.recipes()[*index];
    if (QcCrafting::gridSizeNeeded(recipe) > 2 && !isNearCraftingTable(client)) {
        client.connection->send(QcMsg::ChatMessage{u"%1 needs a crafting table nearby"_s.arg(recipe.name)});
        return;
    }

    const std::optional<QcItemId> result = content.itemId(recipe.resultItem);
    int crafted = 0;
    for (int round = 0; round < std::clamp(request.count, 1, 64) && result; ++round) {
        // All or nothing per round, on a copy so a full inventory leaves everything unchanged.
        QcInventory attempt = client.inventory;
        if (!QcCrafting::consume(content, attempt, recipe, 1) || attempt.add(*result, recipe.resultCount, content) > 0) {
            break;
        }

        client.inventory = attempt;
        ++crafted;
    }

    client.connection->send(QcMsg::ChatMessage{crafted > 0 ? u"Crafted %1 x %2"_s.arg(crafted * recipe.resultCount).arg(recipe.resultItem)
                                                           : u"Missing ingredients or room for %1"_s.arg(recipe.name)});
    send(client);
}

void QcServerInventory::openCraftingTable(QcServerClient &client, QcBlockPos pos)
{
    client.craftingSize = 3;
    client.craftingTable = pos;
    client.connection->send(QcMsg::OpenCrafting{3});
    send(client);
}

void QcServerInventory::closeCrafting(QcServerClient &client)
{
    returnCraftingGrid(client);
    client.craftingSize = 2;
    client.craftingTable.reset();
    send(client);
}

void QcServerInventory::returnCraftingGrid(QcServerClient &client)
{
    for (QcItemStack &cell : client.craftingGrid) {
        if (!cell.isEmpty()) {
            const int left = client.inventory.add(cell.item, cell.count, *m_state.content, cell.damage);
            if (left > 0) {
                throwFromPlayer(client, {cell.item, left, cell.damage});
            }
            cell = {};
        }
    }
}

void QcServerInventory::dropItem(QcServerClient &client, QcMsg::DropItem message)
{
    const int slot = client.inventory.selectedHotbarSlot();
    const QcItemStack stack = client.inventory.slot(slot);
    if (client.dead || stack.isEmpty()) {
        return;
    }

    const int removed = client.inventory.removeFromSlot(slot, message.wholeStack ? stack.count : 1);
    throwFromPlayer(client, {stack.item, removed, stack.damage});
    client.miningPos.reset();
    send(client);
}

void QcServerInventory::dropAll(QcServerClient &client)
{
    const QcVec3 position = client.motion.position + QcVec3{0.0, 1.0, 0.0};
    QList<QcItemStack> stacks = client.inventory.allSlots();
    stacks.append(client.craftingGrid);

    for (const QcItemStack &stack : std::as_const(stacks)) {
        if (!stack.isEmpty()) {
            m_state.items.spawnScattered(stack, position, m_state.newEntityId());
        }
    }

    const int selected = client.inventory.selectedHotbarSlot();
    client.inventory = {};
    client.inventory.setSelectedHotbarSlot(selected);
    client.craftingGrid = QList<QcItemStack>(9);
    send(client);
}

void QcServerInventory::throwFromPlayer(const QcServerClient &client, QcItemStack stack)
{
    const double yaw = qDegreesToRadians(double(client.yaw));
    const double pitch = qDegreesToRadians(double(client.pitch));
    const QcVec3 direction{std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};

    QcItemEntityManager::Drop drop;
    drop.stack = stack;
    drop.position = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight - 0.3, 0.0};
    drop.velocity = direction * throwSpeed + QcVec3{0.0, 2.0, 0.0};
    drop.pickupDelay = QcItemEntityManager::thrownPickupDelay;

    m_state.items.spawn(drop, m_state.newEntityId());
}

void QcServerInventory::exchangeHeld(QcServerClient &client, const QString &item)
{
    const std::optional<QcItemId> result = m_state.content->itemId(item);
    const int slot = client.inventory.selectedHotbarSlot();
    if (!result || client.inventory.slot(slot).isEmpty()) {
        return;
    }

    if (client.inventory.slot(slot).count == 1) {
        client.inventory.setSlot(slot, {*result, 1, 0});
        return;
    }

    client.inventory.removeFromSlot(slot, 1);
    if (const int left = client.inventory.add(*result, 1, *m_state.content); left > 0) {
        throwFromPlayer(client, {*result, left, 0});
    }
}
