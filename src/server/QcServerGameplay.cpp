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
#include <QSet>
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
    // A machine that is replaced by anything but another machine spills its contents.
    if (m_machines.find(pos) && !m_machines.machineOf(block)) {
        dropMachineContents(m_machines.remove(pos));
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
    QcMachineManager::Machine *machine = openMachineOf(client);
    // Inventory, the usable part of the grid and the open machine form one slot space; other grid
    // cells and machine slots without an open machine are invalid.
    const auto usable = [&client, machine](int slot) {
        if (slot < craftingGridSlot) {
            return QcInventory::isValidSlot(slot);
        }
        if (slot >= machineSlot) {
            return machine && slot < machineSlot + QcMachineManager::slotCount;
        }
        const int cell = slot - craftingGridSlot;
        return cell >= 0 && cell < 9 && cell / 3 < client.craftingSize && cell % 3 < client.craftingSize;
    };
    const auto refuse = [&] {
        sendInventory(client);
        sendMachine(client, true);
    };
    if (client.dead || !usable(move.from) || !usable(move.to) || move.to == QcMsg::MoveInventoryItem::machineOutputSlot) {
        refuse();
        return;
    }
    QList<QcItemStack> combined = client.inventory.allSlots();
    combined.append(client.craftingGrid);
    if (machine) {
        combined.append(QList<QcItemStack>(machine->stacks.begin(), machine->stacks.end()));
    }
    QcInventory::moveBetween(combined, move.from, move.to, std::max(0, move.count), *m_content);
    if (machine) {
        // Only fuel goes into the fuel slot, and nothing goes into the output (not even by a swap).
        const QcItemStack &fuel = combined[machineSlot + QcMachineManager::fuelSlot];
        const QcItemStack &output = combined[machineSlot + QcMachineManager::outputSlot];
        const QcItemStack &previousOutput = machine->stacks[QcMachineManager::outputSlot];
        const bool fuelOk = fuel.isEmpty() || m_machines.fuelSeconds(fuel.item) > 0.0;
        const bool outputOk = output.isEmpty() || (output.item == previousOutput.item && output.count <= previousOutput.count);
        if (!fuelOk || !outputOk) {
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
    sendInventory(client);
    if (machine) {
        // Everyone looking into this machine sees the change.
        for (const auto &other : m_clients) {
            if (other->machine == machine->pos) {
                sendMachine(*other, true);
            }
        }
    }
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
    returnCraftingGrid(client);
    client.craftingSize = 2;
    client.craftingTable.reset();
    sendInventory(client);
}

void QcServer::handleConsume(Client &client)
{
    const QcItemDefinition *held = heldItem(client);
    // Full players do not eat (decision E-039).
    if (client.dead || !held || held->food <= 0 || !client.hunger.eat(held->food)) {
        return;
    }
    client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    sendFood(client);
    sendInventory(client);
}

void QcServer::sendFood(Client &client)
{
    client.sentFood = client.hunger.food();
    client.connection->send(QcMsg::PlayerFood{client.hunger.food(), QcHunger::maxFood});
}

QcMachineManager::Machine *QcServer::openMachineOf(const Client &client)
{
    if (!client.machine) {
        return nullptr;
    }
    const std::optional<QcBlockId> block = m_world.blockAt(*client.machine);
    return block && m_machines.machineOf(*block) ? m_machines.find(*client.machine) : nullptr;
}

void QcServer::openMachine(Client &client, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_world.blockAt(pos);
    const QcMachineDefinition *definition = block ? m_machines.machineOf(*block) : nullptr;
    if (!definition) {
        return;
    }
    if (!m_machines.add(pos)) {
        client.connection->send(QcMsg::ChatMessage{u"Too many machines in the world (at most %1)"_s.arg(QcMachineManager::maxMachines)});
        return;
    }
    client.machine = pos;
    client.sentMachine.reset();
    client.connection->send(QcMsg::OpenMachine{pos, definition->category});
    sendMachine(client, true);
}

void QcServer::closeMachine(Client &client, bool notify)
{
    if (!client.machine) {
        return;
    }
    client.machine.reset();
    client.sentMachine.reset();
    if (notify) {
        client.connection->send(QcMsg::CloseMachine{});
    }
}

void QcServer::sendMachine(Client &client, bool force)
{
    const QcMachineManager::Machine *machine = openMachineOf(client);
    if (!machine) {
        return;
    }
    QcMsg::MachineState state;
    state.pos = machine->pos;
    state.stacks = QList<QcItemStack>(machine->stacks.begin(), machine->stacks.end());
    state.burn = float(m_machines.burnFraction(*machine));
    state.progress = float(m_machines.progressFraction(*machine, m_world.blockAt(machine->pos).value_or(qcAirBlockId)));
    // Bars move in steps of one percent; smaller changes are not worth a message.
    const auto percent = [](float value) { return qRound(value * 100.0f); };
    const std::optional<QcMsg::MachineState> &sent = client.sentMachine;
    if (!force && sent && sent->stacks == state.stacks && percent(sent->burn) == percent(state.burn)
        && percent(sent->progress) == percent(state.progress)) {
        return;
    }
    client.sentMachine = state;
    client.connection->send(state);
}

void QcServer::dropMachineContents(const QcMachineManager::Machine &machine)
{
    const QcVec3 centre{machine.pos.x + 0.5, machine.pos.y + 0.5, machine.pos.z + 0.5};
    for (const QcItemStack &stack : machine.stacks) {
        if (!stack.isEmpty()) {
            m_items.spawnScattered(stack, centre, m_nextEntityId++);
        }
    }
}

void QcServer::tickMachines()
{
    const QList<QcMachineManager::Machine> removed = m_machines.tick(m_world, QcPhysics::tickDuration);
    for (const QcMachineManager::Machine &machine : removed) {
        dropMachineContents(machine);
    }
    for (const auto &client : m_clients) {
        if (client->state != Client::State::Playing || !client->machine) {
            continue;
        }
        // The screen closes when the machine is gone, its chunk unloaded or the player walked away.
        if (client->dead || !openMachineOf(*client) || !isWithinReach(*client, *client->machine)) {
            closeMachine(*client, true);
        } else {
            sendMachine(*client);
        }
    }
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
    // Drops fall into the world as items (decision E-028).
    breakBlock(pos, QcMining::canHarvest(definition, held));
    client.hunger.exhaust(QcHunger::mineBlock);
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
    client.swung = true;
    // Using a machine opens its screen, a crafting table its 3 x 3 grid, unless the player sneaks
    // to place against them.
    const std::optional<QcBlockId> clickedBlock = m_world.blockAt(message.target);
    if (clickedBlock && m_machines.machineOf(*clickedBlock) && !client.sneaking && isWithinReach(client, message.target)) {
        openMachine(client, message.target);
        return;
    }
    if (clickedBlock && m_content->block(*clickedBlock).opensCrafting && !client.sneaking && isWithinReach(client, message.target)) {
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
    // Saplings need suitable ground, other cross-shaped plants a solid block below.
    const QcBlockDefinition &placedDefinition = m_content->block(*placed);
    const std::optional<QcBlockId> ground = m_world.blockAt(target.offset(0, -1, 0));
    if ((placedDefinition.sapling.isSapling() && !m_plants.canPlant(*placed, ground))
        || (placedDefinition.shape == QcBlockShape::Cross
            && !(ground && m_content->isValidBlock(*ground) && m_content->block(*ground).solid))) {
        reject();
        return;
    }
    if (placedDefinition.machine.isMachine() && m_machines.isFull()) {
        client.connection->send(QcMsg::ChatMessage{u"Too many machines in the world (at most %1)"_s.arg(QcMachineManager::maxMachines)});
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
    if (placedDefinition.sapling.isSapling()) {
        m_plants.saplingPlaced(target, *placed);
    }
    if (placedDefinition.machine.isMachine()) {
        m_machines.add(target);
    }
    client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    sendInventory(client);
}

void QcServer::breakBlock(QcBlockPos pos, bool drops)
{
    for (QcBlockPos current = pos;; current = current.offset(0, 1, 0)) {
        const std::optional<QcBlockId> previous = m_world.blockAt(current);
        if (!previous || *previous == qcAirBlockId) {
            return;
        }
        const QcBlockDefinition &definition = m_content->block(*previous);
        // Above the broken block only plants that lost their ground break as well.
        if (current != pos && definition.shape != QcBlockShape::Cross) {
            return;
        }
        setBlockAndBroadcast(current, qcAirBlockId);
        if (m_world.blockAt(current) != qcAirBlockId) {
            return;
        }
        if (drops || current != pos) {
            dropBlockItems(definition, current);
        }
        m_plants.blockRemoved(current, *previous, m_world);
    }
}

void QcServer::dropBlockItems(const QcBlockDefinition &definition, QcBlockPos pos)
{
    const QcVec3 centre{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
    for (const QcItemDrop &drop : definition.drops) {
        if (drop.chance < 1.0 && m_random.generateDouble() >= drop.chance) {
            continue;
        }
        if (const std::optional<QcItemId> item = m_content->itemId(drop.item)) {
            m_items.spawnScattered({*item, drop.count, 0}, centre, m_nextEntityId++);
        }
    }
}

void QcServer::throwFromPlayer(const Client &client, QcItemStack stack)
{
    const double yaw = qDegreesToRadians(double(client.yaw));
    const double pitch = qDegreesToRadians(double(client.pitch));
    const QcVec3 direction{std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    QcItemEntityManager::Drop drop;
    drop.stack = stack;
    drop.position = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight - 0.3, 0.0};
    drop.velocity = direction * throwSpeed + QcVec3{0.0, 2.0, 0.0};
    drop.pickupDelay = QcItemEntityManager::thrownPickupDelay;
    m_items.spawn(drop, m_nextEntityId++);
}

void QcServer::handleDropItem(Client &client, QcMsg::DropItem message)
{
    const int slot = client.inventory.selectedHotbarSlot();
    const QcItemStack stack = client.inventory.slot(slot);
    if (client.dead || stack.isEmpty()) {
        return;
    }
    const int removed = client.inventory.removeFromSlot(slot, message.wholeStack ? stack.count : 1);
    throwFromPlayer(client, {stack.item, removed, stack.damage});
    client.miningPos.reset();
    sendInventory(client);
}

QcAppearance QcServer::validAppearance(const QcAppearance &requested) const
{
    QcAppearance result = requested;
    const QcAppearanceDefinition *outfit = m_content->appearance(requested.outfit);
    if (!outfit || outfit->kind != QcAppearanceDefinition::Kind::Outfit || !outfit->playable) {
        // The default outfit, or else any outfit the content offers.
        result.outfit = QcAppearance().outfit;
        const QcAppearanceDefinition *fallback = m_content->appearance(result.outfit);
        if (!fallback || fallback->kind != QcAppearanceDefinition::Kind::Outfit || !fallback->playable) {
            const auto any = std::ranges::find_if(m_content->appearances(), [](const QcAppearanceDefinition &definition) {
                return definition.kind == QcAppearanceDefinition::Kind::Outfit && definition.playable;
            });
            result.outfit = any != m_content->appearances().end() ? any->name : QString();
        }
    }
    const QcAppearanceDefinition *hair = m_content->appearance(requested.hair);
    if (!hair || hair->kind != QcAppearanceDefinition::Kind::Hair || !hair->playable) {
        result.hair.clear();
    }
    for (quint32 *color : {&result.skinColor, &result.hairColor, &result.shirtColor, &result.pantsColor}) {
        *color &= 0xFFFFFFu;
    }
    return result;
}

void QcServer::handleSetAppearance(Client &client, const QcAppearance &appearance)
{
    client.appearance = validAppearance(appearance);
    // Others that know the player get the spawn again with the new look.
    for (const auto &other : m_clients) {
        if (other->knownEntities.contains(client.entityId)) {
            other->staleEntities.insert(client.entityId);
        }
    }
}

void QcServer::returnCraftingGrid(Client &client)
{
    for (QcItemStack &cell : client.craftingGrid) {
        if (!cell.isEmpty()) {
            const int left = client.inventory.add(cell.item, cell.count, *m_content, cell.damage);
            if (left > 0) {
                throwFromPlayer(client, {cell.item, left, cell.damage});
            }
            cell = {};
        }
    }
}

void QcServer::dropInventory(Client &client)
{
    const QcVec3 position = client.motion.position + QcVec3{0.0, 1.0, 0.0};
    QList<QcItemStack> stacks = client.inventory.allSlots();
    stacks.append(client.craftingGrid);
    for (const QcItemStack &stack : std::as_const(stacks)) {
        if (!stack.isEmpty()) {
            m_items.spawnScattered(stack, position, m_nextEntityId++);
        }
    }
    const int selected = client.inventory.selectedHotbarSlot();
    client.inventory = {};
    client.inventory.setSelectedHotbarSlot(selected);
    client.craftingGrid = QList<QcItemStack>(9);
    sendInventory(client);
}

bool QcServer::isLoadedAt(QcBlockPos pos) const
{
    return m_world.chunk(pos.chunk()) != nullptr;
}

void QcServer::tryPlant(const QcItemEntityManager::Item &item)
{
    const QcItemDefinition &definition = m_content->item(item.stack.item);
    const std::optional<QcBlockId> sapling = m_content->blockId(definition.placesBlock);
    if (!sapling || !m_content->block(*sapling).sapling.isSapling()
        || m_random.generateDouble() >= m_content->block(*sapling).sapling.plantChance) {
        return;
    }
    const QcBlockPos pos = QcPhysics::blockContaining(item.motion.position + QcVec3{0.0, 0.05, 0.0});
    const std::optional<QcBlockId> existing = m_world.blockAt(pos);
    if (!existing || !pos.isInsideWorldHeight() || !m_content->block(*existing).replaceable || m_content->block(*existing).liquid
        || !m_plants.canPlant(*sapling, m_world.blockAt(pos.offset(0, -1, 0)))) {
        return;
    }
    setBlockAndBroadcast(pos, *sapling);
    if (m_world.blockAt(pos) == *sapling) {
        m_plants.saplingPlaced(pos, *sapling);
    }
}

void QcServer::tickItemsAndPlants()
{
    const double dt = QcPhysics::tickDuration;
    QList<QcItemEntityManager::PlayerView> players;
    for (const auto &client : m_clients) {
        if (client->state == Client::State::Playing) {
            players.append({client->entityId, client->motion.position, !client->dead});
        }
    }
    QSet<Client *> changed;
    const auto give = [this, &changed](quint32 entityId, QcItemStack stack) {
        for (const auto &client : m_clients) {
            if (client->entityId == entityId && client->state == Client::State::Playing) {
                const int left = client->inventory.add(stack.item, stack.count, *m_content, stack.damage);
                if (left < stack.count) {
                    changed.insert(client.get());
                }
                return left;
            }
        }
        return stack.count;
    };
    const QList<QcItemEntityManager::Item> despawned = m_items.tick(
        m_world, players, dt, [this](const QcVec3 &position) { return isLoadedAt(QcPhysics::blockContaining(position)); }, give,
        [this] { return m_nextEntityId++; });
    for (Client *client : std::as_const(changed)) {
        sendInventory(*client);
    }
    for (const QcItemEntityManager::Item &item : despawned) {
        tryPlant(item);
    }
    m_plants.tick(
        m_world, dt, [this](QcBlockPos pos) { return isLoadedAt(pos); },
        [this](QcBlockPos pos, QcBlockId block) {
            setBlockAndBroadcast(pos, block);
            return m_world.blockAt(pos) == block;
        },
        [this](QcBlockPos pos) { breakBlock(pos, true); });
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
              u"/summon <mob> [count], /kill, /heal, /gamerule keepInventory [true|false], /spawnitem <item> [count], "
              u"/setblock <x> <y> <z> <block> (~ for relative coordinates), /feed [food]"_s);
    } else if (command == u"gamerule"_s && words.size() >= 2 && words[1] == u"keepInventory"_s) {
        if (words.size() == 2) {
            reply(u"keepInventory is %1"_s.arg(m_info.keepInventory ? u"true"_s : u"false"_s));
        } else if (!m_config.allowCheats) {
            reply(u"Cheats are disabled on this server"_s);
        } else if (words[2] == u"true"_s || words[2] == u"false"_s) {
            m_info.keepInventory = words[2] == u"true"_s;
            reply(u"keepInventory is now %1"_s.arg(words[2]));
        } else {
            reply(u"Usage: /gamerule keepInventory [true|false]"_s);
        }
    } else if (!m_config.allowCheats
               && (command == u"give"_s || command == u"time"_s || command == u"summon"_s || command == u"heal"_s
                   || command == u"kill"_s || command == u"spawnitem"_s || command == u"setblock"_s || command == u"feed"_s)) {
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
    } else if (command == u"spawnitem"_s && words.size() >= 2) {
        const QString name = words[1].contains(u':') ? words[1] : u"qtcraft:"_s + words[1];
        const std::optional<QcItemId> item = m_content->itemId(name);
        bool countOk = true;
        const int count = words.size() >= 3 ? words[2].toInt(&countOk) : 1;
        if (!item || !countOk || count < 1 || count > 64) {
            reply(u"Unknown item or invalid count: %1"_s.arg(text));
            return;
        }
        // Two metres in front of the player, on the ground.
        const double yaw = qDegreesToRadians(double(client.yaw));
        const QcVec3 position = client.motion.position + QcVec3{std::sin(yaw) * 2.0, 0.5, -std::cos(yaw) * 2.0};
        m_items.spawnScattered({*item, count, 0}, position, m_nextEntityId++);
        reply(u"Dropped %1 x %2"_s.arg(count).arg(name));
    } else if (command == u"setblock"_s && words.size() >= 5) {
        const QcBlockPos feet = QcPhysics::blockContaining(client.motion.position);
        const auto coordinate = [](const QString &word, int origin, bool &ok) {
            if (word.startsWith(u'~')) {
                ok = true;
                return origin + (word.size() > 1 ? QStringView(word).mid(1).toInt(&ok) : 0);
            }
            return word.toInt(&ok);
        };
        bool okX = false;
        bool okY = false;
        bool okZ = false;
        const QcBlockPos pos{coordinate(words[1], feet.x, okX), coordinate(words[2], feet.y, okY), coordinate(words[3], feet.z, okZ)};
        const QString name = words[4].contains(u':') ? words[4] : u"qtcraft:"_s + words[4];
        const std::optional<QcBlockId> block = m_content->blockId(name);
        if (!okX || !okY || !okZ || !block || !m_world.blockAt(pos) || !pos.isInsideWorldHeight()) {
            reply(u"Usage: /setblock <x> <y> <z> <block> in a loaded chunk"_s);
            return;
        }
        const bool machine = m_machines.machineOf(*block) != nullptr;
        if (machine && !m_machines.find(pos) && m_machines.isFull()) {
            reply(u"Too many machines in the world (at most %1)"_s.arg(QcMachineManager::maxMachines));
            return;
        }
        if (*block == qcAirBlockId) {
            breakBlock(pos, false);
        } else {
            setBlockAndBroadcast(pos, *block);
            if (m_content->block(*block).sapling.isSapling()) {
                m_plants.saplingPlaced(pos, *block);
            }
            if (machine) {
                m_machines.add(pos);
            }
        }
        reply(u"Set %1 at %2 %3 %4"_s.arg(name).arg(pos.x).arg(pos.y).arg(pos.z));
    } else if (command == u"kill"_s) {
        client.hurtTimer = 0.0;
        damagePlayer(client, client.health, {});
    } else if ((command == u"feed"_s || command == u"heal"_s) && client.dead) {
        reply(u"Respawn first"_s);
    } else if (command == u"feed"_s) {
        bool ok = true;
        const int food = words.size() >= 2 ? words[1].toInt(&ok) : QcHunger::maxFood;
        if (!ok || food < 0 || food > QcHunger::maxFood) {
            reply(u"Usage: /feed [0..%1]"_s.arg(QcHunger::maxFood));
            return;
        }
        client.hunger.restore(food, 0.0);
        sendFood(client);
        reply(u"Food is now %1"_s.arg(food));
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
    client.hurtThisTick = true;
    client.hunger.hurt();
    client.hunger.exhaust(QcHunger::damageTaken);
    if (knockback.length() > 0.0) {
        client.motion.velocity = knockback.normalized() * QcMobManager::knockbackSpeed + QcVec3{0.0, 5.0, 0.0};
        client.stateDirty = true;
    }
    if (client.health <= 0.0) {
        client.dead = true;
        client.miningPos.reset();
        m_died.insert(client.entityId);
        client.connection->send(QcMsg::ChatMessage{u"%1 died"_s.arg(client.name)});
        if (!m_info.keepInventory) {
            dropInventory(client);
        }
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
    client.swung = true;
    client.hunger.exhaust(QcHunger::attack);
    const QcItemDefinition *held = heldItem(client);
    const double damage = held ? held->attackDamage : 1.0;
    const QcVec3 lootPosition = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    const std::optional<QList<QcItemDrop>> drops = m_mobs.damage(entityId, damage, client.motion.position);
    if (drops) {
        m_died.insert(entityId);
    }
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
        // Like block drops (decision E-028) the loot falls where the mob died.
        for (const QcItemDrop &drop : *drops) {
            if (drop.chance < 1.0 && m_random.generateDouble() >= drop.chance) {
                continue;
            }
            if (const std::optional<QcItemId> item = m_content->itemId(drop.item)) {
                m_items.spawnScattered({*item, drop.count, 0}, lootPosition, m_nextEntityId++);
            }
        }
    }
}

void QcServer::handleRespawn(Client &client)
{
    if (!client.dead) {
        return;
    }
    client.dead = false;
    client.health = playerMaxHealth;
    client.hunger.reset();
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
        if (!client->dead) {
            // Regeneration at high food, starvation at none (decision E-039).
            const double change = client->hunger.tick(dt, client->health, playerMaxHealth);
            if (change != 0.0) {
                client->health = std::clamp(client->health + change, 0.0, playerMaxHealth);
                client->hurtThisTick = client->hurtThisTick || change < 0.0;
                sendHealth(*client);
            }
        }
        players.append({client->entityId, client->motion.position, !client->dead});
        // Mining swings the arm again and again.
        if (client->miningPos && (m_tickCount - client->miningStartTick) % miningSwingTicks == 0) {
            client->swung = true;
        }
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
    const auto flagsOf = [](const QcEntityMotion &motion, bool swung, bool sneaking, bool sprinting) {
        return quint8((swung ? QcMsg::Swing : 0) | (motion.onGround ? QcMsg::OnGround : 0) | (motion.inWater ? QcMsg::InWater : 0)
                      | (sneaking ? QcMsg::Sneaking : 0) | (sprinting ? QcMsg::Sprinting : 0));
    };
    const auto consider = [&](QcMsg::EntitySpawn spawn, bool hurt) {
        if ((spawn.position - client.motion.position).length() > entityViewDistance) {
            return;
        }
        visible.insert(spawn.id);
        const bool known = client.knownEntities.contains(spawn.id);
        if (!known || client.staleEntities.remove(spawn.id)) {
            client.knownEntities.insert(spawn.id);
            client.connection->send(spawn);
        }
        // A known entity sent again (new look) still moves, swings and flashes in this tick.
        if (known) {
            moves.moves.append({spawn.id, spawn.position, spawn.yaw, spawn.health, hurt, spawn.pitch, spawn.flags, spawn.held});
        }
    };
    for (const QcMobManager::Mob &mob : m_mobs.mobs()) {
        consider({mob.id, mob.type, QString(), mob.motion.position, mob.yaw, mob.health, {}, {}, 0.0f,
                  flagsOf(mob.motion, mob.attackedThisTick, false, false), qcNoItem},
                 mob.hurtThisTick);
    }
    for (const auto &other : m_clients) {
        if (other.get() != &client && other->state == Client::State::Playing && !other->dead) {
            const QcItemStack &held = other->inventory.selectedStack();
            consider({other->entityId, QcMsg::EntitySpawn::playerType, other->name, other->motion.position, other->yaw, other->health,
                      {}, other->appearance, other->pitch, flagsOf(other->motion, other->swung, other->sneaking, other->sprinting),
                      held.isEmpty() ? qcNoItem : held.item},
                     other->hurtThisTick);
        }
    }
    // Items are sent again when their count changed and moved only when their position did.
    for (const QcItemEntityManager::Item &item : m_items.items()) {
        if (item.dormant || (item.motion.position - client.motion.position).length() > itemViewDistance) {
            continue;
        }
        visible.insert(item.id);
        const auto known = client.knownItems.find(item.id);
        if (known == client.knownItems.end() || known->count != item.stack.count) {
            client.connection->send(QcMsg::EntitySpawn{item.id, QcMsg::EntitySpawn::itemType, QString(), item.motion.position,
                                                       0.0f, 0.0, item.stack, {}, 0.0f, 0, qcNoItem});
            client.knownEntities.insert(item.id);
            client.knownItems.insert(item.id, {item.stack.count, item.motion.position});
        } else if ((known->position - item.motion.position).length() > itemMoveThreshold) {
            moves.moves.append({item.id, item.motion.position, 0.0f, 0.0, false, 0.0f, 0, qcNoItem});
            known->position = item.motion.position;
        }
    }
    for (auto it = client.knownEntities.begin(); it != client.knownEntities.end();) {
        if (!visible.contains(*it)) {
            client.connection->send(QcMsg::EntityRemove{*it, m_died.contains(*it)});
            client.knownItems.remove(*it);
            client.staleEntities.remove(*it);
            it = client.knownEntities.erase(it);
        } else {
            ++it;
        }
    }
    if (!moves.moves.isEmpty()) {
        client.connection->send(moves);
    }
}
