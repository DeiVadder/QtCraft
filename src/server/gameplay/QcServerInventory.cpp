// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/gameplay/QcServerInventory.h"

#include "common/content/recipes/QcStonecutting.h"
#include "common/content/registry/QcSoundKeys.h"
#include "common/gameplay/QcArmour.h"
#include "common/gameplay/QcEnchantingTable.h"
#include "common/gameplay/QcPortableContainers.h"
#include "common/gameplay/QcQuickMove.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcServerCrafting.h"
#include "server/gameplay/QcToolWear.h"
#include "server/items/QcServerExperience.h"
#include "server/machines/QcBlockScreens.h"
#include "server/machines/QcServerAnvil.h"
#include "server/machines/QcServerEnchanting.h"
#include "server/machines/QcServerSmithing.h"

#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// Inventory, the usable part of the grid and the `blockSlots` slots of the open block form one slot
// space; other grid cells and block slots are invalid.
bool isUsableSlot(const QcServerClient &client, int slot, int blockSlots)
{
    // A backpack opened from the hand shows only its slots and the hotbar; its own slot is locked
    // (decision E-833).
    if (client.screen.held) {
        const bool hotbar = slot >= 0 && slot < QcInventory::hotbarSize && slot != client.screen.held->slot;
        return hotbar || (slot >= QcServerInventory::machineSlot && slot < QcServerInventory::machineSlot + blockSlots);
    }
    if (slot < QcServerInventory::craftingGridSlot) {
        return QcInventory::isValidSlot(slot);
    }
    if (QcMsg::MoveInventoryItem::isEquipmentSlot(slot)) {
        return true;
    }

    if (slot >= QcServerInventory::machineSlot && slot < QcServerInventory::equipmentSlot) {
        return slot < QcServerInventory::machineSlot + blockSlots;
    }

    const int cell = slot - QcServerInventory::craftingGridSlot;
    return cell >= 0 && cell < 9 && cell / 3 < client.crafting.size && cell % 3 < client.crafting.size;
}

} // namespace

QcServerInventory::QcServerInventory(QcServerState &state, QcBlockScreens &screens, QcServerSounds &sounds)
    : m_state(state)
    , m_screens(screens)
    , m_sounds(sounds)
{
}

void QcServerInventory::send(QcServerClient &client)
{
    QcMsg::InventoryState state;
    state.stacks = client.inventory.allSlots();
    state.selected = client.inventory.selectedHotbarSlot();
    state.craftingGrid = client.crafting.grid;
    state.craftingSize = client.crafting.size;
    state.equipment = client.equipment;

    state.craftingResult = QcServerCrafting::resultOf(m_state, client).value_or(QcItemStack());

    client.connection->send(state);
    m_state.advancements().inventoryChanged(client);
    if (client.screen.stonecutter) {
        client.connection->send(QcMsg::StonecutterState{client.screen.stonecutter->input, client.screen.stonecutter->recipe});
    }
    if (client.screen.enchanting) {
        client.connection->send(QcServerEnchanting::stateOf(m_state, client));
    }
    if (client.screen.anvil) {
        client.connection->send(QcServerAnvil::stateOf(m_state, client));
    }
    if (client.screen.smithing) {
        client.connection->send(QcServerSmithing::stateOf(m_state, client));
    }
}

void QcServerInventory::OpenSlots::storeWorkstationSlots() const
{
    if (stonecutter) {
        stonecutter->setInput(combined.stacks[machineSlot]);
    }
    if (enchanting) {
        enchanting->item = combined.stacks[machineSlot];
        enchanting->lapis = combined.stacks[machineSlot + 1];
    }
    if (anvil) {
        anvil->setLeft(combined.stacks[machineSlot]);
        anvil->right = combined.stacks[machineSlot + 1];
    }
    if (smithing) {
        for (int slot = 0; slot < QcSmithing::slotCount; ++slot) {
            smithing->inputs[static_cast<std::size_t>(slot)] = combined.stacks[machineSlot + slot];
        }
    }
}

QcServerInventory::OpenSlots QcServerInventory::openSlots(QcServerClient &client)
{
    OpenSlots open{m_screens.openMachineOf(client), m_screens.openContainerOf(client), nullptr, nullptr, nullptr, nullptr, 0, {}};
    open.combined.stacks = client.inventory.allSlots();
    open.combined.stacks.append(client.crafting.grid);
    if (open.machine) {
        open.combined.stacks.append(open.machine->stacks);
        open.blockSlots = static_cast<int>(open.machine->stacks.size());
    } else if (open.container) {
        open.combined.stacks.append(*open.container->stacks);
        open.blockSlots = static_cast<int>(open.container->stacks->size());
    } else if (client.screen.stonecutter) {
        // Never open together with a machine or chest (QcServerClient::Screen). Only the input
        // slot: the output is taken through QcServerStonecutter.
        open.stonecutter = &*client.screen.stonecutter;
        open.combined.stacks.append(open.stonecutter->input);
        open.blockSlots = 1;
    } else if (client.screen.enchanting) {
        // The item and lapis slots (decision E-864); never open together with the above.
        open.enchanting = &*client.screen.enchanting;
        open.combined.stacks.append(open.enchanting->item);
        open.combined.stacks.append(open.enchanting->lapis);
        open.blockSlots = 2;
    } else if (client.screen.anvil) {
        // The left and right slots (decision E-866); the output is taken through QcServerAnvil.
        open.anvil = &*client.screen.anvil;
        open.combined.stacks.append(open.anvil->left);
        open.combined.stacks.append(open.anvil->right);
        open.blockSlots = 2;
    } else if (client.screen.smithing) {
        // The template, base and addition slots (decision E-1115); the output is taken through
        // QcServerSmithing.
        open.smithing = &*client.screen.smithing;
        open.combined.stacks.append(QList<QcItemStack>(open.smithing->inputs.begin(), open.smithing->inputs.end()));
        open.blockSlots = QcSmithing::slotCount;
    }
    // The armour slots follow at their own numbers; the cells between them stay unusable.
    open.combined.stacks.resize(equipmentSlot);
    open.combined.stacks.append(QList<QcItemStack>(client.equipment.stacks.begin(), client.equipment.stacks.end()));
    return open;
}

void QcServerInventory::applyOpenSlots(QcServerClient &client, const OpenSlots &open)
{
    if (!armourFits(open.combined.stacks) || !containerAccepts(open) || !enchantingAccepts(open)) {
        send(client);
        m_screens.send(client, true);
        return;
    }

    // Only an inventory whose slots really change tells comparators and hoppers.
    const QList<QcItemStack> before = open.container ? *open.container->stacks : open.machine ? open.machine->stacks : QList<QcItemStack>();
    if (open.machine) {
        if (!machineAccepts(*open.machine, open.combined.stacks)) {
            send(client);
            m_screens.send(client, true);
            return;
        }
        // A player taking from the output gets the experience the machine gathered (decision E-398);
        // what a stand brewed in its bottle slots counts for nothing.
        const auto countOf = [](const QcItemStack &stack) { return stack.isEmpty() ? 0 : stack.count; };
        const bool tookOutput = !brewerOf(*open.machine)
                                && countOf(open.combined.stacks[machineSlot + QcMachineRules::outputSlot]) < countOf(open.machine->stacks[QcMachineRules::outputSlot]);
        const QcItemStack output = open.machine->stacks[QcMachineRules::outputSlot];
        open.machine->stacks = open.combined.stacks.mid(machineSlot, open.blockSlots);
        noteBrewedTaken(client, *open.machine, before);
        if (tookOutput) {
            const int points = QcServerExperience::takeWhole(m_state, std::exchange(open.machine->experience, 0.0));
            QcServerExperience::drop(m_state, points, client.motion.position + QcVec3{0.0, 0.5, 0.0});
            m_state.advancements().trigger(client, {.trigger = QcAdvancementTrigger::ItemSmelted, .item = m_state.content().item(output.item).name});
        }
    }

    for (int i = 0; i < QcInventory::slotCount; ++i) {
        client.inventory.setSlot(i, open.combined.stacks[i]);
    }
    client.crafting.grid = open.combined.stacks.mid(QcInventory::slotCount, 9);
    if (open.container) {
        *open.container->stacks = open.combined.stacks.mid(machineSlot, open.container->stacks->size());
        // A held backpack carries its slots as the data of its item; containerAccepts() checked
        // that they fit (decision E-833).
        if (open.container->heldSlot >= 0) {
            const QcItemStack held = client.inventory.slot(open.container->heldSlot);
            client.inventory.setSlot(open.container->heldSlot, held.tryWithContents(*open.container->stacks).value_or(held));
        }
    }
    open.storeWorkstationSlots();
    QcEquipment equipment;
    std::ranges::copy(open.combined.stacks.mid(equipmentSlot, QcEquipment::slotCount), equipment.stacks.begin());
    setEquipment(client, equipment);

    send(client);
    // Everyone looking into this machine or chest sees the change; comparators and hoppers too.
    if (open.container) {
        m_screens.containerChanged(client, *open.container, *open.container->stacks != before);
    }
    if (open.machine) {
        m_screens.sendToViewers(open.machine->pos);
        if (open.machine->stacks != before) {
            m_state.noteContents(open.machine->pos);
        }
    }
}

void QcServerInventory::moveItem(QcServerClient &client, QcMsg::MoveInventoryItem move)
{
    OpenSlots open = openSlots(client);
    if (client.dead || !isUsableSlot(client, move.from, open.blockSlots) || !isUsableSlot(client, move.to, open.blockSlots) || move.from == move.to
        || (open.machine && !brewerOf(*open.machine) && move.to == QcMsg::MoveInventoryItem::machineOutputSlot)) {
        send(client);
        m_screens.send(client, true);
        return;
    }

    // Both slots are usable and differ (checked above), so the move cannot fail.
    [[maybe_unused]] const bool moved = open.combined.moveBetween(move.from, move.to, std::max(0, move.count), m_state.content());
    applyOpenSlots(client, open);
}

void QcServerInventory::quickMove(QcServerClient &client, QcMsg::QuickMoveItem move)
{
    OpenSlots open = openSlots(client);
    if (client.dead || !isUsableSlot(client, move.slot, open.blockSlots) || open.combined.stacks[move.slot].isEmpty()) {
        send(client);
        m_screens.send(client, true);
        return;
    }

    QcQuickMove::Situation situation;
    situation.containerSlots = open.container ? static_cast<int>(open.container->stacks->size()) : 0;
    // A backpack takes nothing with contents (decision E-832); one opened from the hand moves only
    // between its slots and the hotbar, never into its own slot (decision E-833).
    if (open.container && open.container->portable && !QcPortableContainers::mayHold(m_state.content(), open.combined.stacks[move.slot])) {
        situation.containerSlots = 0;
    }
    situation.hotbarOnly = client.screen.held.has_value();
    situation.machineOpen = open.machine != nullptr || open.stonecutter != nullptr || open.enchanting != nullptr || open.anvil != nullptr
                           || open.smithing != nullptr;
    situation.freeArmourSlot = freeArmourSlotFor(client, open.combined.stacks[move.slot]);
    if (open.stonecutter) {
        // Stone goes into the stonecutter's input like smeltable items into a furnace's.
        const QcItemId item = open.combined.stacks[move.slot].item;
        situation.smeltable = m_state.content().isValidItem(item)
                              && !QcStonecutting::recipesFor(m_state.content(), m_state.content().item(item).name).isEmpty();
    }
    if (open.enchanting) {
        // Enchantable items go into the item slot, lapis lazuli into the second slot.
        const QcItemStack &moved = open.combined.stacks[move.slot];
        situation.smeltable = m_state.content().isValidItem(moved.item) && m_state.content().item(moved.item).enchantability > 0;
        situation.fuel = m_state.content().itemId(QString(QcEnchantingTable::lapisItem)) == moved.item;
    }
    if (open.anvil) {
        // Into the left slot while it is empty, else into the right one.
        situation.smeltable = open.combined.stacks[machineSlot].isEmpty();
        situation.fuel = !situation.smeltable;
    }
    if (open.smithing) {
        // Into the slot the recipes take it in (decision E-1115).
        const std::optional<QcSmithing::Slot> slot = QcSmithing::slotFor(m_state.content(), open.combined.stacks[move.slot]);
        situation.workstationSlot = slot ? machineSlot + static_cast<int>(*slot) : -1;
    }
    if (open.machine) {
        const QcItemId item = open.combined.stacks[move.slot].item;
        const std::optional<QcBlockId> block = m_state.world().blockAt(open.machine->pos);
        const QcMachineDefinition *definition = block ? m_state.machineRules().machineOf(*block) : nullptr;
        situation.smeltable = definition && m_state.machineRules().recipeFor(*definition, item);
        situation.fuel = m_state.machineRules().fuelSeconds(item) > 0.0;
        if (const QcBrewerDefinition *brewer = brewerOf(*open.machine)) {
            const QcBrewingRules &rules = m_state.machineRules().brewing();
            situation.brewing = true;
            situation.smeltable = rules.isIngredient(item);
            situation.fuel = rules.isFuel(*brewer, item);
            situation.bottle = rules.isPotion(item);
        }
    }

    QList<int> targets = QcQuickMove::targets(move.slot, situation);
    targets.removeIf([&](int slot) { return !isUsableSlot(client, slot, open.blockSlots); });
    open.combined.moveInto(move.slot, targets, m_state.content());
    applyOpenSlots(client, open);
}

bool QcServerInventory::containerAccepts(const OpenSlots &open) const
{
    if (!open.container || !open.container->portable) {
        return true;
    }

    // Nothing with contents goes into a backpack (decision E-832); a held one must take its new
    // slots as data, else nothing changes and nothing is lost.
    const QList<QcItemStack> cells = open.combined.stacks.mid(machineSlot, open.container->stacks->size());
    if (!QcPortableContainers::mayHoldAll(m_state.content(), cells)) {
        return false;
    }
    return open.container->heldSlot < 0 || open.combined.stacks[open.container->heldSlot].tryWithContents(cells).has_value();
}

bool QcServerInventory::enchantingAccepts(const OpenSlots &open) const
{
    if (!open.enchanting) {
        return true;
    }
    const QcItemStack &item = open.combined.stacks[machineSlot];
    const QcItemStack &lapis = open.combined.stacks[machineSlot + 1];
    return (item.isEmpty() || item.count == 1) && (lapis.isEmpty() || m_state.content().itemId(QString(QcEnchantingTable::lapisItem)) == lapis.item);
}

void QcServerInventory::noteBrewedTaken(QcServerClient &client, const QcBlockEntities::Machine &machine, const QList<QcItemStack> &before)
{
    if (!brewerOf(machine)) {
        return;
    }
    // A potion taken out of a bottle slot counts as brewed, like the reference (decision E-1107).
    for (int slot = QcBrewerDefinition::firstBottleSlot; slot < QcBrewerDefinition::slotCount && slot < before.size(); ++slot) {
        const QcItemStack &was = before[slot];
        const bool taken = !was.isEmpty() && (machine.stacks[slot].isEmpty() || machine.stacks[slot].item != was.item);
        if (taken && m_state.content().potionCatalog().potionOf(was.item)) {
            m_state.advancements().trigger(client, {.trigger = QcAdvancementTrigger::PotionBrewed, .item = m_state.content().item(was.item).name});
        }
    }
}

const QcBrewerDefinition *QcServerInventory::brewerOf(const QcBlockEntities::Machine &machine) const
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(machine.pos);
    const QcMachineDefinition *definition = block ? m_state.machineRules().machineOf(*block) : nullptr;
    return definition && definition->brewing ? &*definition->brewing : nullptr;
}

bool QcServerInventory::machineAccepts(const QcBlockEntities::Machine &machine, const QList<QcItemStack> &combined) const
{
    // What stays behind is part of what was there, data and all.
    const auto partOf = [](const QcItemStack &now, const QcItemStack &before) {
        return !before.isEmpty() && now.withCount(before.count) == before && now.count <= before.count;
    };

    if (const QcBrewerDefinition *brewer = brewerOf(machine)) {
        // Every slot of a stand keeps to its kind; what lay there may stay.
        for (int slot = 0; slot < QcBrewerDefinition::slotCount; ++slot) {
            const QcItemStack &stack = combined[machineSlot + slot];
            if (!stack.isEmpty() && !partOf(stack, machine.stacks[slot]) && !m_state.machineRules().brewing().accepts(*brewer, slot, stack)) {
                return false;
            }
        }
        return true;
    }

    // Only fuel goes into the fuel slot, and nothing goes into the output (not even by a swap).
    // An emptied container left behind by burnt fuel may stay or be taken out; fuel carries no contents.
    const QcItemStack &fuel = combined[machineSlot + QcMachineRules::fuelSlot];
    const QcItemStack &previousFuel = machine.stacks[QcMachineRules::fuelSlot];
    const QcItemStack &output = combined[machineSlot + QcMachineRules::outputSlot];
    const QcItemStack &previousOutput = machine.stacks[QcMachineRules::outputSlot];

    const bool fuelOk = fuel.isEmpty() || (m_state.machineRules().fuelSeconds(fuel.item) > 0.0 && !fuel.hasContents()) || partOf(fuel, previousFuel);
    const bool outputOk = output.isEmpty() || partOf(output, previousOutput);
    return fuelOk && outputOk;
}

void QcServerInventory::giveOrThrow(QcServerClient &client, const QcItemStack &stack)
{
    if (stack.isEmpty()) {
        return;
    }

    const int left = client.inventory.add(stack, m_state.content());
    if (left > 0) {
        throwFromPlayer(client, stack.withCount(left));
    }
}

void QcServerInventory::dropItem(QcServerClient &client, QcMsg::DropItem message)
{
    const int slot = client.inventory.selectedHotbarSlot();
    const QcItemStack stack = client.inventory.slot(slot);
    // The slot of a backpack open in the hand is locked (decision E-833).
    if (client.dead || stack.isEmpty() || (client.screen.held && client.screen.held->slot == slot)) {
        return;
    }

    const int removed = client.inventory.removeFromSlot(slot, message.wholeStack ? stack.count : 1);
    throwFromPlayer(client, stack.withCount(removed));
    client.mining.pos.reset();
    send(client);
}

void QcServerInventory::dropAll(QcServerClient &client)
{
    const QcVec3 position = client.motion.position + QcVec3{0.0, 1.0, 0.0};
    QList<QcItemStack> stacks = client.inventory.allSlots();
    stacks.append(client.crafting.grid);
    stacks.append(client.screen.takeWorkstationStacks());
    stacks.append(QList<QcItemStack>(client.equipment.stacks.begin(), client.equipment.stacks.end()));

    for (const QcItemStack &stack : std::as_const(stacks)) {
        if (!stack.isEmpty()) {
            m_state.entities().items().spawnScattered(stack, position, m_state.entities().newId());
        }
    }

    const int selected = client.inventory.selectedHotbarSlot();
    client.inventory = {};
    client.inventory.setSelectedHotbarSlot(selected);
    client.crafting.grid = QList<QcItemStack>(9);
    // Dying takes nothing off: no equip sound, others just see the player without armour.
    client.equipment = {};
    m_state.entities().markStale(client.id);
    send(client);
}

void QcServerInventory::closeHeldScreen(QcServerClient &client)
{
    if (client.screen.held) {
        m_screens.close(client, true);
    }
}

void QcServerInventory::returnScreenInputs(QcServerClient &client)
{
    const QList<QcItemStack> inputs = client.screen.takeWorkstationStacks();

    for (const QcItemStack &input : std::as_const(inputs)) {
        if (input.isEmpty()) {
            continue;
        }
        // Catalogue items that do not fit go into the bin rather than into the world (decision E-283).
        if (QcGameModes::rules(client.gameMode).itemCatalogue) {
            client.inventory.add(input, m_state.content());
        } else {
            giveOrThrow(client, input);
        }
    }
}

void QcServerInventory::throwFromPlayer(const QcServerClient &client, const QcItemStack &stack)
{
    const double yaw = qDegreesToRadians(static_cast<double>(client.yaw));
    const double pitch = qDegreesToRadians(static_cast<double>(client.pitch));
    const QcVec3 direction{std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};

    QcItemEntityManager::Drop drop;
    drop.stack = stack;
    drop.position = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight - 0.3, 0.0};
    drop.velocity = direction * throwSpeed + QcVec3{0.0, 2.0, 0.0};
    drop.pickupDelay = QcItemEntityManager::thrownPickupDelay;

    m_state.entities().items().spawn(drop, m_state.entities().newId());
}

void QcServerInventory::exchangeHeld(QcServerClient &client, const QString &item)
{
    const std::optional<QcItemId> result = m_state.content().itemId(item);
    const int slot = client.inventory.selectedHotbarSlot();
    if (!result || client.inventory.slot(slot).isEmpty()) {
        return;
    }

    // Creative mode: the held item stays, the result joins the inventory once (decision E-282).
    if (client.rules().infiniteItems) {
        if (client.inventory.countOf(*result) == 0) {
            client.inventory.add(*result, 1, m_state.content());
        }
        return;
    }

    if (client.inventory.slot(slot).count == 1) {
        client.inventory.setSlot(slot, {*result, 1, 0});
        return;
    }

    client.inventory.removeFromSlot(slot, 1);
    if (const int left = client.inventory.add(*result, 1, m_state.content()); left > 0) {
        throwFromPlayer(client, {*result, left, 0});
    }
}

bool QcServerInventory::armourFits(const QList<QcItemStack> &combined) const
{
    for (std::size_t i = 0; i < QcEquipment::slotCount; ++i) {
        const QcItemStack &stack = combined[equipmentSlot + static_cast<qsizetype>(i)];
        if (!stack.isEmpty() && !QcArmour::fits(m_state.content(), stack, static_cast<QcEquipmentSlot>(i))) {
            return false;
        }
    }
    return true;
}

int QcServerInventory::freeArmourSlotFor(const QcServerClient &client, const QcItemStack &stack) const
{
    const QcArmourDefinition *armour = stack.isEmpty() ? nullptr : QcArmour::of(m_state.content(), stack.item);
    if (!armour || !client.equipment.at(armour->slot).isEmpty()) {
        return -1;
    }
    return equipmentSlot + static_cast<int>(armour->slot);
}

void QcServerInventory::setEquipment(QcServerClient &client, const QcEquipment &equipment)
{
    if (client.equipment == equipment) {
        return;
    }

    // A piece put on or taken off makes a sound; wear alone does not.
    const auto items = [](const QcEquipment &worn) {
        std::array<QcItemId, QcEquipment::slotCount> ids{};
        std::ranges::transform(worn.stacks, ids.begin(), [](const QcItemStack &stack) { return stack.item; });
        return ids;
    };
    if (items(client.equipment) != items(equipment)) {
        m_sounds.play(QString::fromLatin1(QcSoundKeys::armourEquip), client.motion.position + QcVec3{0.0, 1.0, 0.0});
    }

    client.equipment = equipment;
    m_state.entities().markStale(client.id);
}

void QcServerInventory::equipHeld(QcServerClient &client)
{
    const int slot = client.inventory.selectedHotbarSlot();
    const QcItemStack held = client.inventory.slot(slot);
    const QcArmourDefinition *armour = held.isEmpty() ? nullptr : QcArmour::of(m_state.content(), held.item);
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
    m_state.entities().markStale(client.id);
    if (broke) {
        m_sounds.play(QString::fromLatin1(QcSoundKeys::itemBreak), client.motion.position + QcVec3{0.0, 1.0, 0.0});
    }
    send(client);
}

void QcServerInventory::wearHeld(QcServerClient &client)
{
    const QcItemDefinition *held = m_state.heldItem(client);
    // Tools wear like when mining, except in creative mode (decision E-282).
    if (!held || held->durability <= 0 || client.rules().infiniteItems) {
        return;
    }

    const int slot = client.inventory.selectedHotbarSlot();
    client.inventory.setSlot(slot, QcToolWear::worn(m_state, client.inventory.slot(slot)));
    send(client);
}
