#include "QcServerInventory.h"

#include "QcArmour.h"
#include "QcChatText.h"
#include "QcCrafting.h"
#include "QcPhysics.h"
#include "QcQuickMove.h"
#include "QcRepair.h"
#include "QcServerMachines.h"
#include "QcServerSounds.h"
#include "QcServerState.h"
#include "QcSoundKeys.h"

#include <QtMath>

#include <algorithm>
#include <array>
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
    if (QcMsg::MoveInventoryItem::isEquipmentSlot(slot)) {
        return true;
    }

    if (slot >= QcServerInventory::machineSlot && slot < QcServerInventory::equipmentSlot) {
        return (machine && slot < QcServerInventory::machineSlot + QcMachineManager::slotCount)
               || (container && slot < QcServerInventory::machineSlot + container->stacks.size());
    }

    const int cell = slot - QcServerInventory::craftingGridSlot;
    return cell >= 0 && cell < 9 && cell / 3 < client.craftingSize && cell % 3 < client.craftingSize;
}

} // namespace

QcServerInventory::QcServerInventory(QcServerState &state, QcServerMachines &machines, QcServerSounds &sounds)
    : m_state(state)
    , m_machines(machines)
    , m_sounds(sounds)
{
}

void QcServerInventory::send(QcServerClient &client)
{
    QcMsg::InventoryState state;
    state.stacks = client.inventory.allSlots();
    state.selected = client.inventory.selectedHotbarSlot();
    state.craftingGrid = client.craftingGrid;
    state.craftingSize = client.craftingSize;
    state.equipment = client.equipment;

    if (const std::optional<qsizetype> recipe = craftingMatch(client)) {
        const QcRecipeDefinition &definition = m_state.content->recipes()[*recipe];
        if (const std::optional<QcItemId> item = m_state.content->itemId(definition.resultItem)) {
            state.craftingResult = {*item, definition.resultCount, 0};
        }
    } else if (const std::optional<QcItemStack> repaired = QcRepair::result(*m_state.content, craftingView(client))) {
        state.craftingResult = *repaired;
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

QcServerInventory::OpenSlots QcServerInventory::openSlots(QcServerClient &client)
{
    OpenSlots open{m_machines.openMachineOf(client), m_machines.openContainerOf(client), {}};
    open.combined = client.inventory.allSlots();
    open.combined.append(client.craftingGrid);
    if (open.machine) {
        open.combined.append(QList<QcItemStack>(open.machine->stacks.begin(), open.machine->stacks.end()));
    } else if (open.container) {
        open.combined.append(open.container->stacks);
    }
    // The armour slots follow at their own numbers; the cells between them stay unusable.
    open.combined.resize(equipmentSlot);
    open.combined.append(QList<QcItemStack>(client.equipment.stacks.begin(), client.equipment.stacks.end()));
    return open;
}

bool QcServerInventory::applyOpenSlots(QcServerClient &client, const OpenSlots &open)
{
    if (!armourFits(open.combined)) {
        send(client);
        m_machines.send(client, true);
        return false;
    }

    if (open.machine) {
        if (!machineAccepts(*open.machine, open.combined)) {
            send(client);
            m_machines.send(client, true);
            return false;
        }
        for (int slot = 0; slot < QcMachineManager::slotCount; ++slot) {
            open.machine->stacks[std::size_t(slot)] = open.combined[machineSlot + slot];
        }
    }

    for (int i = 0; i < QcInventory::slotCount; ++i) {
        client.inventory.setSlot(i, open.combined[i]);
    }
    client.craftingGrid = open.combined.mid(QcInventory::slotCount, 9);
    if (open.container) {
        open.container->stacks = open.combined.mid(machineSlot, open.container->stacks.size());
    }
    QcEquipment equipment;
    std::ranges::copy(open.combined.mid(equipmentSlot, QcEquipment::slotCount), equipment.stacks.begin());
    setEquipment(client, equipment);

    send(client);
    // Everyone looking into this machine or chest sees the change.
    if (open.container) {
        m_machines.sendToViewers(open.container->pos);
    }
    if (open.machine) {
        m_machines.sendToViewers(open.machine->pos);
    }
    return true;
}

void QcServerInventory::moveItem(QcServerClient &client, QcMsg::MoveInventoryItem move)
{
    OpenSlots open = openSlots(client);
    if (client.dead || !isUsableSlot(client, move.from, open.machine, open.container)
        || !isUsableSlot(client, move.to, open.machine, open.container)
        || (open.machine && move.to == QcMsg::MoveInventoryItem::machineOutputSlot)) {
        send(client);
        m_machines.send(client, true);
        return;
    }

    QcInventory::moveBetween(open.combined, move.from, move.to, std::max(0, move.count), *m_state.content);
    applyOpenSlots(client, open);
}

void QcServerInventory::quickMove(QcServerClient &client, QcMsg::QuickMoveItem move)
{
    if (move.slot == QcMsg::QuickMoveItem::craftingResultSlot) {
        craftAll(client);
        return;
    }

    OpenSlots open = openSlots(client);
    if (client.dead || !isUsableSlot(client, move.slot, open.machine, open.container) || open.combined[move.slot].isEmpty()) {
        send(client);
        m_machines.send(client, true);
        return;
    }

    QcQuickMove::Situation situation;
    situation.containerSlots = open.container ? int(open.container->stacks.size()) : 0;
    situation.machineOpen = open.machine != nullptr;
    situation.freeArmourSlot = freeArmourSlotFor(client, open.combined[move.slot]);
    if (open.machine) {
        const QcItemId item = open.combined[move.slot].item;
        const std::optional<QcBlockId> block = m_state.world.blockAt(open.machine->pos);
        const QcMachineDefinition *definition = block ? m_state.machines.machineOf(*block) : nullptr;
        situation.smeltable = definition && m_state.machines.recipeFor(*definition, item);
        situation.fuel = m_state.machines.fuelSeconds(item) > 0.0;
    }

    QcInventory::moveInto(open.combined, move.slot, QcQuickMove::targets(move.slot, situation), *m_state.content);
    applyOpenSlots(client, open);
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

QcServerInventory::CraftOutcome QcServerInventory::craftOnce(QcServerClient &client)
{
    if (client.craftingSize == 3 && !isNearCraftingTable(client)) {
        return CraftOutcome::TooFar;
    }

    const std::optional<qsizetype> recipe = craftingMatch(client);
    if (!client.dead && !recipe) {
        return repairOnce(client);
    }
    if (client.dead) {
        return CraftOutcome::NoRecipe;
    }

    const QcContentRegistry &content = *m_state.content;
    const QcRecipeDefinition &definition = content.recipes()[*recipe];
    const std::optional<QcItemId> item = content.itemId(definition.resultItem);
    QcInventory result = client.inventory;
    if (!item || result.add(*item, definition.resultCount, content) > 0) {
        return CraftOutcome::NoRoom;
    }

    client.inventory = result;
    takeOneOfEachCell(client);
    return CraftOutcome::Crafted;
}

QcServerInventory::CraftOutcome QcServerInventory::repairOnce(QcServerClient &client)
{
    const std::optional<QcItemStack> repaired = QcRepair::result(*m_state.content, craftingView(client));
    if (!repaired) {
        return CraftOutcome::NoRecipe;
    }

    QcInventory result = client.inventory;
    if (result.add(repaired->item, 1, *m_state.content, repaired->damage) > 0) {
        return CraftOutcome::NoRoom;
    }

    client.inventory = result;
    takeOneOfEachCell(client);
    return CraftOutcome::Crafted;
}

void QcServerInventory::takeOneOfEachCell(QcServerClient &client)
{
    for (int row = 0; row < client.craftingSize; ++row) {
        for (int column = 0; column < client.craftingSize; ++column) {
            QcItemStack &cell = client.craftingGrid[row * 3 + column];
            if (!cell.isEmpty() && --cell.count <= 0) {
                cell = {};
            }
        }
    }
}

void QcServerInventory::takeCraftingResult(QcServerClient &client)
{
    finishCrafting(client, craftOnce(client), false);
}

void QcServerInventory::craftAll(QcServerClient &client)
{
    // As often as the grid and the inventory allow, at most a full stack of rounds.
    CraftOutcome outcome = CraftOutcome::NoRecipe;
    bool crafted = false;
    for (int round = 0; round < maxCraftAllRounds; ++round) {
        outcome = craftOnce(client);
        if (outcome != CraftOutcome::Crafted) {
            break;
        }
        crafted = true;
    }
    finishCrafting(client, outcome, crafted);
}

void QcServerInventory::finishCrafting(QcServerClient &client, CraftOutcome outcome, bool craftedBefore)
{
    if (outcome == CraftOutcome::TooFar) {
        closeCrafting(client);
        return;
    }
    if (outcome == CraftOutcome::NoRoom && !craftedBefore) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Not enough room in the inventory")));
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
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown recipe %1"),
            {request.recipe}));
        return;
    }

    const QcRecipeDefinition &recipe = content.recipes()[*index];
    if (QcCrafting::gridSizeNeeded(recipe) > 2 && !isNearCraftingTable(client)) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "%1 needs a crafting table nearby"),
            {recipe.name}));
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

    if (crafted > 0) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Crafted %1 x %2"),
                                                    {QString::number(crafted * recipe.resultCount), recipe.resultItem}));
    } else {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Missing ingredients or room for %1"), {recipe.name}));
    }
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
            giveOrThrow(client, cell);
            cell = {};
        }
    }
}

void QcServerInventory::giveOrThrow(QcServerClient &client, QcItemStack stack)
{
    if (stack.isEmpty()) {
        return;
    }

    const int left = client.inventory.add(stack.item, stack.count, *m_state.content, stack.damage);
    if (left > 0) {
        throwFromPlayer(client, {stack.item, left, stack.damage});
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
    stacks.append(QList<QcItemStack>(client.equipment.stacks.begin(), client.equipment.stacks.end()));

    for (const QcItemStack &stack : std::as_const(stacks)) {
        if (!stack.isEmpty()) {
            m_state.items.spawnScattered(stack, position, m_state.newEntityId());
        }
    }

    const int selected = client.inventory.selectedHotbarSlot();
    client.inventory = {};
    client.inventory.setSelectedHotbarSlot(selected);
    client.craftingGrid = QList<QcItemStack>(9);
    // Dying takes nothing off: no equip sound, others just see the player without armour.
    client.equipment = {};
    m_state.markStale(client.entityId);
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

bool QcServerInventory::armourFits(const QList<QcItemStack> &combined) const
{
    for (std::size_t i = 0; i < QcEquipment::slotCount; ++i) {
        const QcItemStack &stack = combined[equipmentSlot + qsizetype(i)];
        if (!stack.isEmpty() && !QcArmour::fits(*m_state.content, stack, QcEquipmentSlot(i))) {
            return false;
        }
    }
    return true;
}

int QcServerInventory::freeArmourSlotFor(const QcServerClient &client, QcItemStack stack) const
{
    const QcArmourDefinition *armour = stack.isEmpty() ? nullptr : QcArmour::of(*m_state.content, stack.item);
    if (!armour || !client.equipment.at(armour->slot).isEmpty()) {
        return -1;
    }
    return equipmentSlot + int(armour->slot);
}

void QcServerInventory::setEquipment(QcServerClient &client, const QcEquipment &equipment)
{
    if (client.equipment == equipment) {
        return;
    }

    // A piece put on or taken off makes a sound; wear alone does not.
    const auto items = [](const QcEquipment &worn) {
        std::array<QcItemId, QcEquipment::slotCount> ids{};
        std::ranges::transform(worn.stacks, ids.begin(), [](QcItemStack stack) { return stack.item; });
        return ids;
    };
    if (items(client.equipment) != items(equipment)) {
        m_sounds.play(QString::fromLatin1(QcSoundKeys::armourEquip), client.motion.position + QcVec3{0.0, 1.0, 0.0});
    }

    client.equipment = equipment;
    m_state.markStale(client.entityId);
}

void QcServerInventory::equipHeld(QcServerClient &client)
{
    const int slot = client.inventory.selectedHotbarSlot();
    const QcItemStack held = client.inventory.slot(slot);
    const QcArmourDefinition *armour = held.isEmpty() ? nullptr : QcArmour::of(*m_state.content, held.item);
    if (client.dead || !armour || held.count != 1) {
        return;
    }

    // What was worn there goes into the hand.
    QcEquipment equipment = client.equipment;
    client.inventory.setSlot(slot, equipment.at(armour->slot));
    equipment.at(armour->slot) = held;
    setEquipment(client, equipment);
    send(client);
}

void QcServerInventory::equipmentWorn(QcServerClient &client, const QcEquipment &equipment, bool broke)
{
    client.equipment = equipment;
    m_state.markStale(client.entityId);
    if (broke) {
        m_sounds.play(QString::fromLatin1(QcSoundKeys::itemBreak), client.motion.position + QcVec3{0.0, 1.0, 0.0});
    }
    send(client);
}
