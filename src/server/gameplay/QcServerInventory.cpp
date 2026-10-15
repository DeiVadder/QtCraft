#include "QcServerInventory.h"

#include "QcArmour.h"
#include "QcPhysics.h"
#include "QcQuickMove.h"
#include "QcServerCrafting.h"
#include "QcBlockScreens.h"
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
bool isUsableSlot(const QcServerClient &client, int slot, const QcBlockEntities::Machine *machine,
                  const std::optional<QcBlockScreens::OpenContainer> &container)
{
    if (slot < QcServerInventory::craftingGridSlot) {
        return QcInventory::isValidSlot(slot);
    }
    if (QcMsg::MoveInventoryItem::isEquipmentSlot(slot)) {
        return true;
    }

    if (slot >= QcServerInventory::machineSlot && slot < QcServerInventory::equipmentSlot) {
        return (machine && slot < QcServerInventory::machineSlot + QcMachineRules::slotCount)
               || (container && slot < QcServerInventory::machineSlot + container->stacks->size());
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

    state.craftingResult = QcServerCrafting::resultOf(m_state.content(), client).value_or(QcItemStack());

    client.connection->send(state);
}

QcServerInventory::OpenSlots QcServerInventory::openSlots(QcServerClient &client)
{
    OpenSlots open{m_screens.openMachineOf(client), m_screens.openContainerOf(client), {}};
    open.combined = client.inventory.allSlots();
    open.combined.append(client.crafting.grid);
    if (open.machine) {
        open.combined.append(QList<QcItemStack>(open.machine->stacks.begin(), open.machine->stacks.end()));
    } else if (open.container) {
        open.combined.append(*open.container->stacks);
    }
    // The armour slots follow at their own numbers; the cells between them stay unusable.
    open.combined.resize(equipmentSlot);
    open.combined.append(QList<QcItemStack>(client.equipment.stacks.begin(), client.equipment.stacks.end()));
    return open;
}

void QcServerInventory::applyOpenSlots(QcServerClient &client, const OpenSlots &open)
{
    if (!armourFits(open.combined)) {
        send(client);
        m_screens.send(client, true);
        return;
    }

    // Only an inventory whose slots really change tells comparators and hoppers.
    const QList<QcItemStack> before = open.container ? *open.container->stacks
                                      : open.machine ? QList<QcItemStack>(open.machine->stacks.begin(), open.machine->stacks.end())
                                                     : QList<QcItemStack>();
    if (open.machine) {
        if (!machineAccepts(*open.machine, open.combined)) {
            send(client);
            m_screens.send(client, true);
            return;
        }
        for (int slot = 0; slot < QcMachineRules::slotCount; ++slot) {
            open.machine->stacks[std::size_t(slot)] = open.combined[machineSlot + slot];
        }
    }

    for (int i = 0; i < QcInventory::slotCount; ++i) {
        client.inventory.setSlot(i, open.combined[i]);
    }
    client.crafting.grid = open.combined.mid(QcInventory::slotCount, 9);
    if (open.container) {
        *open.container->stacks = open.combined.mid(machineSlot, open.container->stacks->size());
    }
    QcEquipment equipment;
    std::ranges::copy(open.combined.mid(equipmentSlot, QcEquipment::slotCount), equipment.stacks.begin());
    setEquipment(client, equipment);

    send(client);
    // Everyone looking into this machine or chest sees the change; comparators and hoppers too.
    if (open.container) {
        m_screens.containerChanged(*open.container, *open.container->stacks != before);
    }
    if (open.machine) {
        m_screens.sendToViewers(open.machine->pos);
        if (QList<QcItemStack>(open.machine->stacks.begin(), open.machine->stacks.end()) != before) {
            m_state.noteContents(open.machine->pos);
        }
    }
}

void QcServerInventory::moveItem(QcServerClient &client, QcMsg::MoveInventoryItem move)
{
    OpenSlots open = openSlots(client);
    if (client.dead || !isUsableSlot(client, move.from, open.machine, open.container)
        || !isUsableSlot(client, move.to, open.machine, open.container) || move.from == move.to
        || (open.machine && move.to == QcMsg::MoveInventoryItem::machineOutputSlot)) {
        send(client);
        m_screens.send(client, true);
        return;
    }

    // Both slots are usable and differ (checked above), so the move cannot fail.
    [[maybe_unused]] const bool moved = QcInventory::moveBetween(open.combined, move.from, move.to, std::max(0, move.count), m_state.content());
    applyOpenSlots(client, open);
}

void QcServerInventory::quickMove(QcServerClient &client, QcMsg::QuickMoveItem move)
{
    OpenSlots open = openSlots(client);
    if (client.dead || !isUsableSlot(client, move.slot, open.machine, open.container) || open.combined[move.slot].isEmpty()) {
        send(client);
        m_screens.send(client, true);
        return;
    }

    QcQuickMove::Situation situation;
    situation.containerSlots = open.container ? int(open.container->stacks->size()) : 0;
    situation.machineOpen = open.machine != nullptr;
    situation.freeArmourSlot = freeArmourSlotFor(client, open.combined[move.slot]);
    if (open.machine) {
        const QcItemId item = open.combined[move.slot].item;
        const std::optional<QcBlockId> block = m_state.world().blockAt(open.machine->pos);
        const QcMachineDefinition *definition = block ? m_state.machineRules().machineOf(*block) : nullptr;
        situation.smeltable = definition && m_state.machineRules().recipeFor(*definition, item);
        situation.fuel = m_state.machineRules().fuelSeconds(item) > 0.0;
    }

    QcInventory::moveInto(open.combined, move.slot, QcQuickMove::targets(move.slot, situation), m_state.content());
    applyOpenSlots(client, open);
}

bool QcServerInventory::machineAccepts(const QcBlockEntities::Machine &machine, const QList<QcItemStack> &combined) const
{
    // Only fuel goes into the fuel slot, and nothing goes into the output (not even by a swap).
    // An emptied container left behind by burnt fuel may stay or be taken out.
    const QcItemStack &fuel = combined[machineSlot + QcMachineRules::fuelSlot];
    const QcItemStack &previousFuel = machine.stacks[QcMachineRules::fuelSlot];
    const QcItemStack &output = combined[machineSlot + QcMachineRules::outputSlot];
    const QcItemStack &previousOutput = machine.stacks[QcMachineRules::outputSlot];

    const bool fuelOk = fuel.isEmpty() || m_state.machineRules().fuelSeconds(fuel.item) > 0.0
                        || (fuel.item == previousFuel.item && fuel.damage == previousFuel.damage && fuel.count <= previousFuel.count);
    const bool outputOk = output.isEmpty() || (output.item == previousOutput.item && output.count <= previousOutput.count);
    return fuelOk && outputOk;
}

void QcServerInventory::giveOrThrow(QcServerClient &client, QcItemStack stack)
{
    if (stack.isEmpty()) {
        return;
    }

    const int left = client.inventory.add(stack.item, stack.count, m_state.content(), stack.damage);
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
    client.mining.pos.reset();
    send(client);
}

void QcServerInventory::dropAll(QcServerClient &client)
{
    const QcVec3 position = client.motion.position + QcVec3{0.0, 1.0, 0.0};
    QList<QcItemStack> stacks = client.inventory.allSlots();
    stacks.append(client.crafting.grid);
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
        const QcItemStack &stack = combined[equipmentSlot + qsizetype(i)];
        if (!stack.isEmpty() && !QcArmour::fits(m_state.content(), stack, QcEquipmentSlot(i))) {
            return false;
        }
    }
    return true;
}

int QcServerInventory::freeArmourSlotFor(const QcServerClient &client, QcItemStack stack) const
{
    const QcArmourDefinition *armour = stack.isEmpty() ? nullptr : QcArmour::of(m_state.content(), stack.item);
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
