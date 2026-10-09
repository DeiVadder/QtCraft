// Gameplay of QcServer: mining, placing, inventory, combat, health, entity sync and commands.

#include "QcServer.h"

#include "QcBlockRules.h"
#include "QcCrafting.h"
#include "QcDayCycle.h"
#include "QcMining.h"
#include "QcPhysics.h"
#include "QcProtocol.h"
#include "QcRaycast.h"
#include "QcServerClient.h"
#include "QcSoundKeys.h"

#include <QLoggingCategory>
#include <QScopeGuard>
#include <QSet>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcServerGameplay, "qtcraft.server")

} // namespace

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
    // So does a chest replaced by anything but another storage block; a storage block of another
    // size keeps what fits and spills the rest.
    reconcileContainer(pos);
    for (const auto &client : m_clients) {
        if (client->state == Client::State::Playing && client->sentChunks.contains(pos.chunk())) {
            client->connection->send(QcMsg::BlockUpdate{pos, block});
        }
    }
    queueNeighbourUpdates(pos, block != qcAirBlockId);
}

void QcServer::reconcileContainer(QcBlockPos pos)
{
    if (!m_containers.find(pos)) {
        return;
    }
    const std::optional<QcBlockId> block = m_world.blockAt(pos);
    const int slotCount = block ? m_containers.slotsOf(*block) : 0;
    if (slotCount == 0) {
        dropStacks(m_containers.remove(pos).stacks, pos);
    } else {
        dropStacks(m_containers.resize(pos, slotCount), pos);
    }
}

void QcServer::queueNeighbourUpdates(QcBlockPos pos, bool self)
{
    if (self) {
        queueBlockUpdate(pos);
    }
    for (const QcBlockFace face : qcAllBlockFaces) {
        queueBlockUpdate(qcNeighbour(pos, face));
    }
}

void QcServer::queueBlockUpdate(QcBlockPos pos)
{
    if (!pos.isInsideWorldHeight()) {
        return;
    }
    if (m_commandUpdateMode) {
        if (!m_queuedPlayerUpdates.contains(pos) && !m_queuedCommandUpdates.contains(pos)) {
            if (std::cmp_greater_equal(m_commandUpdates.size(), m_config.maxCommandBlockUpdates)) {
                // Bounded memory and save size; a block whose update was dropped is checked again
                // by the next change next to it.
                if (m_droppedBlockUpdates++ == 0) {
                    qCWarning(lcServerGameplay) << "Too many pending command updates; dropping further ones";
                }
                return;
            }
            m_queuedCommandUpdates.insert(pos);
            m_commandUpdates.push_back(pos);
        }
    } else if (!m_queuedPlayerUpdates.contains(pos)) {
        // Also when the command queue holds it: the player's change must not wait behind a backlog.
        m_queuedPlayerUpdates.insert(pos);
        m_playerUpdates.push_back(pos);
    }
}

void QcServer::processBlockUpdates()
{
    for (int budget = maxBlockUpdatesPerTick; budget > 0; --budget) {
        const bool fromCommand = m_playerUpdates.empty();
        std::deque<QcBlockPos> &queue = fromCommand ? m_commandUpdates : m_playerUpdates;
        if (queue.empty()) {
            break;
        }
        const QcBlockPos pos = queue.front();
        queue.pop_front();
        (fromCommand ? m_queuedCommandUpdates : m_queuedPlayerUpdates).remove(pos);
        ++m_processedBlockUpdates;
        const std::optional<QcBlockId> block = m_world.blockAt(pos);
        if (!block) {
            waitForChunk(pos.chunk(), pos);
            continue;
        }
        if (*block == qcAirBlockId) {
            continue;
        }
        // Cascades stay in the queue they come from.
        m_commandUpdateMode = fromCommand;
        if (QcFallingBlockManager::shouldFall(m_world, pos)) {
            startFalling(pos, *block);
        } else if (QcLiquidFlow::flows(*m_content, *block)) {
            // Lava meeting water hardens at once; flowing waits for the liquid's next step.
            if (const std::optional<QcBlockId> turned = QcLiquidFlow::reaction(m_world, pos, *block)) {
                setBlockAndBroadcast(pos, *turned);
                playSound(QString::fromLatin1(QcSoundKeys::liquidHiss), {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5});
            } else {
                m_liquids.schedule(m_world, pos, m_tickCount);
            }
        } else if (!QcBlockRules::isSupported(m_world, pos, *block)) {
            // A door half without its partner was cut off by a command or a removed mod: the item
            // went with the other half.
            const bool lone = m_content->block(*block).door && !QcBlockRules::hasDoorPartner(m_world, pos, *block);
            breakBlock(pos, !lone);
        } else if (const QcBlockId connected = QcBlockRules::connectedState(m_world, pos, *block); connected != *block) {
            setBlockAndBroadcast(pos, connected);
        }
        m_commandUpdateMode = false;
    }
}

void QcServer::waitForChunk(QcChunkPos chunk, QcBlockPos pos)
{
    if (m_waitingUpdateCount < maxWaitingBlockUpdates) {
        QSet<QcBlockPos> &waiting = m_waitingUpdates[chunk];
        const qsizetype before = waiting.size();
        waiting.insert(pos);
        m_waitingUpdateCount += waiting.size() - before;
    }
}

void QcServer::processLiquids()
{
    if (!m_liquids.hasDueSteps(m_tickCount)) {
        return;
    }
    m_liquidSoundPlayed = false;
    // Flood cascades queue behind the updates of player actions, like commands.
    m_commandUpdateMode = true;
    const QcLiquidFlow::TickResult result = m_liquids.tick(m_tickCount, m_world, [this](QcBlockPos pos, QcBlockId block) {
        const std::optional<QcBlockId> existing = m_world.blockAt(pos);
        if (!existing || !m_content->isValidBlock(*existing)) {
            return;
        }
        const QcBlockDefinition &before = m_content->block(*existing);
        const QcVec3 centre{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
        if (*existing != qcAirBlockId && !before.liquid && before.washable) {
            breakBlock(pos, true);
        }
        setBlockAndBroadcast(pos, block);
        const QcBlockDefinition &after = m_content->block(block);
        if (before.liquid && !after.liquid && block != qcAirBlockId) {
            playSound(QString::fromLatin1(QcSoundKeys::liquidHiss), centre);
        } else if (after.liquid && !after.flow.sound.isEmpty() && !m_liquidSoundPlayed && !m_content->isSameBlock(*existing, block)
                   && m_soundRandom.generateDouble() < 0.25) {
            m_liquidSoundPlayed = true;
            playSound(after.flow.sound, centre);
        }
    });
    m_commandUpdateMode = false;
    for (const QcBlockPos &pos : result.unloaded) {
        waitForChunk(pos.chunk(), pos);
    }
}

void QcServer::startFalling(QcBlockPos pos, QcBlockId block)
{
    if (!m_falling.isFull()) {
        setBlockAndBroadcast(pos, qcAirBlockId);
        m_falling.start(pos, block, m_nextEntityId++);
        return;
    }
    const std::optional<QcBlockPos> rest = QcFallingBlockManager::restingCell(m_world, pos);
    setBlockAndBroadcast(pos, qcAirBlockId);
    if (rest) {
        landBlock(block, *rest, true);
    }
}

void QcServer::landBlock(QcBlockId block, QcBlockPos cell, bool placed)
{
    const QcBlockDefinition &definition = m_content->block(block);
    // A block landing inside a player or mob would trap it, so it drops as items instead.
    QList<QcAabb> boxes;
    const QcVec3 corner{double(cell.x), double(cell.y), double(cell.z)};
    for (const QcAabb &box : m_content->state(block).collision) {
        boxes.append(box.moved(corner));
    }
    if (placed && QcFallingBlockManager::isPassable(m_world, cell) && !overlapsBody(boxes)) {
        setBlockAndBroadcast(cell, block);
        if (m_world.blockAt(cell) == block) {
            playBlockSound(definition, QcBlockSoundAction::Place, cell);
            return;
        }
    }
    dropBlockItems(definition, cell);
}

void QcServer::tickFallingBlocks()
{
    if (m_falling.all().isEmpty()) {
        return;
    }
    const QList<QcFallingBlockManager::Landing> landed = m_falling.tick(
        m_world, QcPhysics::tickDuration, [this](const QcVec3 &position) { return isLoadedAt(QcPhysics::blockContaining(position)); });
    for (const QcFallingBlockManager::Landing &landing : landed) {
        landBlock(landing.block, landing.pos, landing.placed);
    }
}

void QcServer::applyContactDamage()
{
    if (!m_world.mayHaveContactDamage()) {
        return;
    }
    for (const auto &client : m_clients) {
        if (client->state != Client::State::Playing || client->dead) {
            continue;
        }
        const double damage = m_world.contactDamageIn(QcPhysics::boxAt(client->motion.position, QcPhysics::playerWidth, QcPhysics::playerHeight));
        if (damage > 0.0) {
            damagePlayer(*client, damage, {});
        }
    }
    // Hurting may remove a mob, so the damage is collected before any is dealt.
    QList<std::pair<quint32, double>> touched;
    for (const QcMobManager::Mob &mob : m_mobs.mobs()) {
        const QcMobDefinition &definition = m_content->mobs()[mob.type];
        if (const double damage = m_world.contactDamageIn(QcPhysics::boxAt(mob.motion.position, definition.width, definition.height));
            damage > 0.0) {
            touched.append({mob.id, damage});
        }
    }
    for (const auto &[id, damage] : std::as_const(touched)) {
        const QcMobManager::Mob *mob = m_mobs.find(id);
        if (!mob) {
            continue;
        }
        const QString sound = m_content->mobs()[mob->type].deathSound;
        const QcVec3 position = mob->motion.position;
        if (m_mobs.hurt(id, damage)) {
            playSound(sound, position);
            // Burnt creatures leave no loot.
            m_died.insert(id);
        }
    }
    m_items.removeWhere([this](const QcItemEntityManager::Item &item) {
        return m_world.contactDamageIn(QcPhysics::boxAt(item.motion.position, QcItemEntityManager::itemSize, QcItemEntityManager::itemSize)) > 0.0;
    });
}

void QcServer::handleLoadedChunks()
{
    const QList<QcChunkPos> loaded = m_world.takeNewlyLoaded();
    for (const QcChunkPos &chunk : loaded) {
        if (!m_world.chunk(chunk)) {
            continue;
        }
        m_commandUpdateMode = true;
        if (const auto waiting = m_waitingUpdates.constFind(chunk); waiting != m_waitingUpdates.constEnd()) {
            for (const QcBlockPos &pos : *waiting) {
                queueBlockUpdate(pos);
            }
            m_waitingUpdateCount -= waiting->size();
            m_waitingUpdates.erase(waiting);
        }
        constexpr std::array<QcBlockFace, 4> sides = {QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West};
        for (const QcBlockFace side : sides) {
            const QcFaceOffset offset = qcFaceOffset(side);
            const QcChunkPos neighbour{chunk.x + offset.dx, chunk.z + offset.dz};
            if (m_world.chunk(neighbour)) {
                queueBorderUpdates(chunk, side);
                queueBorderUpdates(neighbour, QcBlockRules::opposite(side));
            }
        }
        m_commandUpdateMode = false;
    }
}

void QcServer::queueBorderUpdates(QcChunkPos chunkPos, QcBlockFace towards)
{
    const std::shared_ptr<const QcChunk> chunk = m_world.chunk(chunkPos);
    if (!chunk) {
        return;
    }
    // Only blocks whose state depends on the horizontal neighbour: connections, wall mounts and
    // flowing liquids.
    const auto reacts = [this](QcBlockId id) {
        if (!m_content->isValidBlock(id)) {
            return false;
        }
        const QcBlockDefinition &definition = m_content->block(id);
        return !definition.connectsTo.isEmpty() || definition.support == QcBlockSupport::Attached
               || (definition.liquid && definition.flow.flows());
    };
    // Borders of oceans and lakes repeat the same pair of states over and over; the check whether
    // a still source can flow across is made once per pair.
    struct SourceCheck
    {
        QcBlockId block = qcAirBlockId;
        std::optional<QcBlockId> neighbour;
        bool stays = false;
    };
    std::optional<SourceCheck> lastCheck;
    const QcFaceOffset offset = qcFaceOffset(towards);
    constexpr int last = QcWorld::chunkSize - 1;
    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        const QcChunkSection &section = chunk->section(index);
        if (std::ranges::none_of(section.palette(), reacts)) {
            continue;
        }
        for (int y = 0; y < QcWorld::sectionHeight; ++y) {
            for (int along = 0; along < QcWorld::chunkSize; ++along) {
                const int x = offset.dx != 0 ? (offset.dx > 0 ? last : 0) : along;
                const int z = offset.dz != 0 ? (offset.dz > 0 ? last : 0) : along;
                const QcBlockId block = section.block(x, y, z);
                if (!reacts(block)) {
                    continue;
                }
                const QcBlockPos pos{chunkPos.x * QcWorld::chunkSize + x, index * QcWorld::sectionHeight + y,
                                     chunkPos.z * QcWorld::chunkSize + z};
                // A still source only matters where it can flow across the border or reacts with a
                // liquid there (oceans would otherwise queue every border cell on each load). Only the
                // neighbour across the border is looked at: the other sides lie inside a chunk that
                // was already loaded, where the source settled when it was generated or last updated.
                if (QcLiquidFlow::flows(*m_content, block)) {
                    const std::optional<QcBlockId> neighbour = m_world.blockAt(qcNeighbour(pos, towards));
                    if (!lastCheck || lastCheck->block != block || lastCheck->neighbour != neighbour) {
                        const QcBlockDefinition *across =
                            neighbour && m_content->isValidBlock(*neighbour) ? &m_content->block(*neighbour) : nullptr;
                        const bool reacts = across && across->liquid && !m_content->isSameBlock(*neighbour, block)
                                            && std::ranges::contains(m_content->block(block).flow.reactions, across->name,
                                                                     &QcLiquidReaction::with);
                        const bool stays = QcLiquidFlow::isSource(*m_content, block) && !reacts
                                           && !QcLiquidFlow::canReplace(*m_content, neighbour, QcLiquidFlow::stateWith(*m_content, block, 1, false));
                        lastCheck = SourceCheck{block, neighbour, stays};
                    }
                    if (lastCheck->stays) {
                        continue;
                    }
                }
                queueBlockUpdate(pos);
            }
        }
    }
}

bool QcServer::overlapsBody(const QList<QcAabb> &boxes) const
{
    const auto hits = [&boxes](const QcAabb &body) {
        return std::ranges::any_of(boxes, [&body](const QcAabb &box) { return body.intersects(box); });
    };
    for (const auto &client : m_clients) {
        if (client->state == Client::State::Playing && !client->dead
            && hits(QcPhysics::boxAt(client->motion.position, QcPhysics::playerWidth, QcPhysics::playerHeight))) {
            return true;
        }
    }
    const QList<QcMobDefinition> &definitions = m_content->mobs();
    for (const QcMobManager::Mob &mob : m_mobs.mobs()) {
        if (mob.type >= 0 && mob.type < definitions.size()
            && hits(QcPhysics::boxAt(mob.motion.position, definitions[mob.type].width, definitions[mob.type].height))) {
            return true;
        }
    }
    return false;
}

void QcServer::toggleBlock(QcBlockPos pos, QcBlockId block)
{
    const QString open = m_content->property(block, u"open"_s) == u"true"_s ? u"false"_s : u"true"_s;
    const QcBlockDefinition &definition = m_content->block(block);
    QList<std::pair<QcBlockPos, QcBlockId>> changes{{pos, m_content->withProperty(block, u"open"_s, open)}};
    if (definition.door) {
        const QcBlockPos other = QcBlockRules::otherDoorHalf(m_world, pos, block);
        const std::optional<QcBlockId> partner = m_world.blockAt(other);
        if (partner && m_content->isSameBlock(*partner, block)) {
            changes.append({other, m_content->withProperty(*partner, u"open"_s, open)});
        }
    }
    // Swinging shut (or open) must not trap anyone inside the new shape.
    QList<QcAabb> boxes;
    for (const auto &[at, state] : std::as_const(changes)) {
        const QcVec3 corner{double(at.x), double(at.y), double(at.z)};
        for (const QcAabb &box : m_content->state(state).collision) {
            boxes.append(box.moved(corner));
        }
    }
    if (overlapsBody(boxes)) {
        return;
    }
    for (const auto &[at, state] : std::as_const(changes)) {
        setBlockAndBroadcast(at, state);
    }
    playBlockSound(definition, QcBlockSoundAction::Place, pos);
}

void QcServer::playSound(const QString &key, const QcVec3 &position, float volume, float pitch)
{
    const QcSoundDefinition *sound = key.isEmpty() ? nullptr : m_content->sound(key);
    if (!sound) {
        return;
    }
    const double range = sound->range * std::max(1.0, double(volume));
    const QcMsg::SoundEvent event{key, position, volume, pitch};
    for (const auto &client : m_clients) {
        if (client->state == Client::State::Playing && (client->motion.position - position).length() <= range) {
            client->connection->send(event);
        }
    }
}

void QcServer::playBlockSound(const QcBlockDefinition &definition, QcBlockSoundAction action, QcBlockPos pos)
{
    playSound(QcSoundKeys::blockSound(definition, action), {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5});
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
    QcContainerManager::Container *container = openContainerOf(client);
    // Inventory, the usable part of the grid and the open machine or chest form one slot space;
    // other grid cells and block slots without an open block are invalid.
    const auto usable = [&client, machine, container](int slot) {
        if (slot < craftingGridSlot) {
            return QcInventory::isValidSlot(slot);
        }
        if (slot >= machineSlot) {
            return (machine && slot < machineSlot + QcMachineManager::slotCount)
                   || (container && slot < machineSlot + container->stacks.size());
        }
        const int cell = slot - craftingGridSlot;
        return cell >= 0 && cell < 9 && cell / 3 < client.craftingSize && cell % 3 < client.craftingSize;
    };
    const auto refuse = [&] {
        sendInventory(client);
        sendMachine(client, true);
    };
    if (client.dead || !usable(move.from) || !usable(move.to) || (machine && move.to == QcMsg::MoveInventoryItem::machineOutputSlot)) {
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
    QcInventory::moveBetween(combined, move.from, move.to, std::max(0, move.count), *m_content);
    if (machine) {
        // Only fuel goes into the fuel slot, and nothing goes into the output (not even by a swap).
        // An emptied container left behind by burnt fuel may stay or be taken out.
        const QcItemStack &fuel = combined[machineSlot + QcMachineManager::fuelSlot];
        const QcItemStack &previousFuel = machine->stacks[QcMachineManager::fuelSlot];
        const QcItemStack &output = combined[machineSlot + QcMachineManager::outputSlot];
        const QcItemStack &previousOutput = machine->stacks[QcMachineManager::outputSlot];
        const bool fuelOk = fuel.isEmpty() || m_machines.fuelSeconds(fuel.item) > 0.0
                            || (fuel.item == previousFuel.item && fuel.damage == previousFuel.damage && fuel.count <= previousFuel.count);
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
    if (container) {
        container->stacks = combined.mid(machineSlot, container->stacks.size());
    }
    sendInventory(client);
    if (container) {
        for (const auto &other : m_clients) {
            if (other->machine == container->pos) {
                sendMachine(*other, true);
            }
        }
    }
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
    int crafted = 0;
    for (int round = 0; round < std::clamp(request.count, 1, 64) && result; ++round) {
        // All or nothing per round, on a copy so a full inventory leaves everything unchanged.
        QcInventory attempt = client.inventory;
        if (!QcCrafting::consume(*m_content, attempt, recipe, 1) || attempt.add(*result, recipe.resultCount, *m_content) > 0) {
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
    playSound(QString::fromLatin1(QcSoundKeys::playerEat), client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0});
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

QcContainerManager::Container *QcServer::openContainerOf(const Client &client)
{
    if (!client.machine) {
        return nullptr;
    }
    const std::optional<QcBlockId> block = m_world.blockAt(*client.machine);
    return block && m_containers.slotsOf(*block) > 0 ? m_containers.find(*client.machine) : nullptr;
}

void QcServer::dropStacks(const QList<QcItemStack> &stacks, QcBlockPos pos)
{
    const QcVec3 centre{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
    for (const QcItemStack &stack : stacks) {
        if (!stack.isEmpty()) {
            m_items.spawnScattered(stack, centre, m_nextEntityId++);
        }
    }
}

void QcServer::openMachine(Client &client, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_world.blockAt(pos);
    if (const int slotCount = block ? m_containers.slotsOf(*block) : 0; slotCount > 0) {
        if (!m_containers.add(pos, slotCount)) {
            client.connection->send(QcMsg::ChatMessage{u"Too many chests in the world (at most %1)"_s.arg(QcContainerManager::maxContainers)});
            return;
        }
        client.machine = pos;
        client.sentMachine.reset();
        client.connection->send(QcMsg::OpenMachine{pos, {}});
        sendMachine(client, true);
        return;
    }
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
    if (const QcContainerManager::Container *container = openContainerOf(client)) {
        QcMsg::MachineState state;
        state.pos = container->pos;
        state.stacks = container->stacks;
        if (force || !client.sentMachine || client.sentMachine->stacks != state.stacks) {
            client.sentMachine = state;
            client.connection->send(state);
        }
        return;
    }
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
    for (const QcMachineManager::Machine &machine : m_machines.all()) {
        if (machine.burnRemaining <= 0.0 || !isLoadedAt(machine.pos)
            || m_soundRandom.generateDouble() >= QcPhysics::tickDuration / machineSoundSeconds) {
            continue;
        }
        const std::optional<QcBlockId> block = m_world.blockAt(machine.pos);
        if (const QcMachineDefinition *definition = block ? m_machines.machineOf(*block) : nullptr) {
            playSound(definition->sound, {machine.pos.x + 0.5, machine.pos.y + 0.5, machine.pos.z + 0.5});
        }
    }
    for (const auto &client : m_clients) {
        if (client->state != Client::State::Playing || !client->machine) {
            continue;
        }
        // The screen closes when the machine is gone, its chunk unloaded or the player walked away.
        if (client->dead || (!openMachineOf(*client) && !openContainerOf(*client)) || !isWithinReach(*client, *client->machine)) {
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

void QcServer::handleUseItem(Client &client, const QcMsg::UseItem &message)
{
    client.swung = true;
    // Using a machine opens its screen, a crafting table its 3 x 3 grid, unless the player sneaks
    // to place against them.
    const std::optional<QcBlockId> clickedBlock = m_world.blockAt(message.target);
    const bool storage = clickedBlock && m_containers.slotsOf(*clickedBlock) > 0;
    if (clickedBlock && (m_machines.machineOf(*clickedBlock) || storage) && !client.sneaking && isWithinReach(client, message.target)) {
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
    if (clickedBlock && m_content->block(*clickedBlock).toggles && !client.sneaking && isWithinReach(client, message.target)) {
        toggleBlock(message.target, *clickedBlock);
        return;
    }
    const QcItemDefinition *held = heldItem(client);
    if (held && held->scoops) {
        if (!scoopLiquid(client, message.target)) {
            if (const std::optional<QcBlockId> block = m_world.blockAt(message.target)) {
                client.connection->send(QcMsg::BlockUpdate{message.target, *block});
            }
            sendInventory(client);
        }
        return;
    }
    const std::optional<QcBlockId> clicked = m_world.blockAt(message.target);
    const std::optional<QcBlockId> placed = held ? m_content->blockId(held->placesBlock) : std::nullopt;
    const bool clickedSolid = clicked && m_content->isValidBlock(*clicked) && !m_content->block(*clicked).liquid;
    const std::optional<QcBlockRules::Placement> placement =
        placed && clickedSolid && *clicked != qcAirBlockId
            ? QcBlockRules::placement(m_world, *placed, message.target, message.face, message.hit, client.yaw)
            : std::nullopt;
    const QcBlockPos target = placement ? placement->pos : qcNeighbour(message.target, message.face);
    const std::optional<QcBlockId> existing = m_world.blockAt(target);
    const auto reject = [&] {
        for (const QcBlockPos pos : {target, target.offset(0, 1, 0)}) {
            if (const std::optional<QcBlockId> block = m_world.blockAt(pos)) {
                client.connection->send(QcMsg::BlockUpdate{pos, *block});
            }
        }
        sendInventory(client);
    };
    if (!placement || !existing || !isWithinReach(client, target)) {
        reject();
        return;
    }
    // Saplings need suitable ground.
    const QcBlockDefinition &placedDefinition = m_content->block(placement->state);
    const std::optional<QcBlockId> ground = m_world.blockAt(target.offset(0, -1, 0));
    if (placedDefinition.sapling.isSapling() && !m_plants.canPlant(*placed, ground)) {
        reject();
        return;
    }
    if (placedDefinition.containerSlots > 0 && m_containers.isFull()) {
        client.connection->send(QcMsg::ChatMessage{u"Too many chests in the world (at most %1)"_s.arg(QcContainerManager::maxContainers)});
        reject();
        return;
    }
    if (placedDefinition.machine.isMachine() && m_machines.isFull()) {
        client.connection->send(QcMsg::ChatMessage{u"Too many machines in the world (at most %1)"_s.arg(QcMachineManager::maxMachines)});
        reject();
        return;
    }
    // Nobody, player or mob, may end up inside what is placed (both halves of a door).
    QList<QcAabb> boxes;
    const QcVec3 corner{double(target.x), double(target.y), double(target.z)};
    for (const QcAabb &box : m_content->state(placement->state).collision) {
        boxes.append(box.moved(corner));
        if (placedDefinition.door) {
            boxes.append(box.moved(corner + QcVec3{0.0, 1.0, 0.0}));
        }
    }
    if (overlapsBody(boxes)) {
        reject();
        return;
    }
    setBlockAndBroadcast(target, placement->state);
    if (placedDefinition.door) {
        setBlockAndBroadcast(target.offset(0, 1, 0), m_content->withProperty(placement->state, u"half"_s, u"top"_s));
    }
    playBlockSound(placedDefinition, QcBlockSoundAction::Place, target);
    if (placedDefinition.sapling.isSapling()) {
        m_plants.saplingPlaced(target, placement->state);
    }
    if (placedDefinition.machine.isMachine()) {
        m_machines.add(target);
    }
    if (placedDefinition.containerSlots > 0) {
        m_containers.add(target, placedDefinition.containerSlots);
    }
    if (!held->emptiesTo.isEmpty() && m_content->itemId(held->emptiesTo)) {
        // A poured bucket stays in the hand, empty.
        playSound(QString::fromLatin1(QcSoundKeys::bucketEmpty), {target.x + 0.5, target.y + 0.5, target.z + 0.5});
        exchangeHeld(client, held->emptiesTo);
    } else {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    sendInventory(client);
}

void QcServer::exchangeHeld(Client &client, const QString &item)
{
    const std::optional<QcItemId> result = m_content->itemId(item);
    const int slot = client.inventory.selectedHotbarSlot();
    if (!result || client.inventory.slot(slot).isEmpty()) {
        return;
    }
    if (client.inventory.slot(slot).count == 1) {
        client.inventory.setSlot(slot, {*result, 1, 0});
        return;
    }
    client.inventory.removeFromSlot(slot, 1);
    if (const int left = client.inventory.add(*result, 1, *m_content); left > 0) {
        throwFromPlayer(client, {*result, left, 0});
    }
}

bool QcServer::scoopLiquid(Client &client, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_world.blockAt(pos);
    if (!block || !m_content->isValidBlock(*block) || !isWithinReach(client, pos)) {
        return false;
    }
    const QcBlockDefinition &definition = m_content->block(*block);
    if (!definition.liquid || definition.flow.bucket.isEmpty() || !QcLiquidFlow::isSource(*m_content, *block)
        || !m_content->itemId(definition.flow.bucket)) {
        return false;
    }
    setBlockAndBroadcast(pos, qcAirBlockId);
    playSound(QString::fromLatin1(QcSoundKeys::bucketFill), {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5});
    exchangeHeld(client, definition.flow.bucket);
    sendInventory(client);
    return true;
}

void QcServer::handleUseEntity(Client &client, quint32 entityId)
{
    client.swung = true;
    const QcMobManager::Mob *mob = m_mobs.find(entityId);
    const QcItemDefinition *held = heldItem(client);
    if (!mob || !held) {
        return;
    }
    const QcMobDefinition &definition = m_content->mobs()[mob->type];
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    if (definition.catchWith != held->name || !m_content->itemId(definition.catchItem)
        || (centre - eye).length() > attackReach + definition.width + 1.0) {
        return;
    }
    m_mobs.remove(entityId);
    playSound(QString::fromLatin1(QcSoundKeys::bottleCatch), centre);
    exchangeHeld(client, definition.catchItem);
    sendInventory(client);
}

void QcServer::breakBlock(QcBlockPos pos, bool drops)
{
    const std::optional<QcBlockId> previous = m_world.blockAt(pos);
    if (!previous || *previous == qcAirBlockId) {
        return;
    }
    const QcBlockDefinition &definition = m_content->block(*previous);
    // A slab broken out of a double slab drops both halves.
    const bool doubleSlab = m_content->property(*previous, u"type"_s) == u"double"_s;
    const bool pairedDoor = definition.door && QcBlockRules::hasDoorPartner(m_world, pos, *previous);
    setBlockAndBroadcast(pos, qcAirBlockId);
    if (m_world.blockAt(pos) != qcAirBlockId) {
        return;
    }
    if (pairedDoor) {
        breakBlock(QcBlockRules::otherDoorHalf(m_world, pos, *previous), drops);
    }
    playBlockSound(definition, QcBlockSoundAction::Break, pos);
    // The upper half of a door drops nothing: the lower half carries the door item.
    const bool doorTop = definition.door && m_content->property(*previous, u"half"_s) == u"top"_s;
    if (drops && !doorTop) {
        dropBlockItems(definition, pos);
        if (doubleSlab) {
            dropBlockItems(definition, pos);
        }
    }
    m_plants.blockRemoved(pos, *previous, m_world);
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
                    playSound(QString::fromLatin1(QcSoundKeys::itemPickup), client->motion.position, 0.6f);
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
              u"/setblock <x> <y> <z> <block>[states], /fill <x1> <y1> <z1> <x2> <y2> <z2> <block> (~ for relative coordinates), "
              u"/feed [food], /locatebiome <biome>"_s);
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
                   || command == u"kill"_s || command == u"spawnitem"_s || command == u"setblock"_s || command == u"fill"_s || command == u"feed"_s)) {
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
    } else if ((command == u"setblock"_s && words.size() >= 5) || (command == u"fill"_s && words.size() >= 8)) {
        const QcBlockPos feet = QcPhysics::blockContaining(client.motion.position);
        const auto coordinate = [](const QString &word, int origin, bool &ok) {
            if (word.startsWith(u'~')) {
                ok = true;
                return origin + (word.size() > 1 ? QStringView(word).mid(1).toInt(&ok) : 0);
            }
            return word.toInt(&ok);
        };
        bool ok = true;
        const auto position = [&](int first) {
            bool okX = false;
            bool okY = false;
            bool okZ = false;
            const QcBlockPos pos{coordinate(words[first], feet.x, okX), coordinate(words[first + 1], feet.y, okY),
                                 coordinate(words[first + 2], feet.z, okZ)};
            ok = ok && okX && okY && okZ;
            return pos;
        };
        const bool fill = command == u"fill"_s;
        const QcBlockPos from = position(1);
        const QcBlockPos to = fill ? position(4) : from;
        const QString &text = words[fill ? 7 : 4];
        // "name" or "name[property=value,...]"; without states, connections follow the neighbours.
        const QString name = text.section(u'[', 0, 0).contains(u':') ? text : u"qtcraft:"_s + text;
        const bool explicitState = name.contains(u'[');
        QStringList problems;
        const std::optional<QcBlockId> block = m_content->stateId(name, &problems);
        if (block && !problems.isEmpty()) {
            reply(problems.join(u"; "_s));
            return;
        }
        const QcBlockPos low{std::min(from.x, to.x), std::min(from.y, to.y), std::min(from.z, to.z)};
        const QcBlockPos high{std::max(from.x, to.x), std::max(from.y, to.y), std::max(from.z, to.z)};
        const qint64 volume = qint64(high.x - low.x + 1) * (high.y - low.y + 1) * (high.z - low.z + 1);
        if (!ok || !block || !low.isInsideWorldHeight() || !high.isInsideWorldHeight() || volume > maxFillBlocks) {
            reply(u"Usage: /setblock <x> <y> <z> <block>, /fill <x1> <y1> <z1> <x2> <y2> <z2> <block> (at most %1 blocks; "
                  u"block may carry states: name[property=value,...]) in loaded chunks"_s.arg(maxFillBlocks));
            return;
        }
        const bool machine = m_machines.machineOf(*block) != nullptr;
        // The changes and their cascades queue behind the updates of player actions.
        m_commandUpdateMode = true;
        const auto normalMode = qScopeGuard([this] { m_commandUpdateMode = false; });
        int changed = 0;
        for (int y = low.y; y <= high.y; ++y) {
            for (int z = low.z; z <= high.z; ++z) {
                for (int x = low.x; x <= high.x; ++x) {
                    const QcBlockPos pos{x, y, z};
                    if (!m_world.blockAt(pos) || (machine && !m_machines.find(pos) && m_machines.isFull())) {
                        continue;
                    }
                    if (*block == qcAirBlockId) {
                        breakBlock(pos, false);
                    } else {
                        setBlockAndBroadcast(pos, explicitState ? *block : QcBlockRules::connectedState(m_world, pos, *block));
                        if (m_content->block(*block).sapling.isSapling()) {
                            m_plants.saplingPlaced(pos, *block);
                        }
                        if (machine) {
                            m_machines.add(pos);
                        }
                    }
                    ++changed;
                }
            }
        }
        if (changed == 0) {
            reply(u"Nothing set: the chunks are not loaded%1"_s.arg(machine ? u" or there are too many machines"_s : QString()));
            return;
        }
        reply(fill ? u"Filled %1 blocks with %2"_s.arg(changed).arg(name)
                   : u"Set %1 at %2 %3 %4"_s.arg(name).arg(from.x).arg(from.y).arg(from.z));
    } else if (command == u"locatebiome"_s && words.size() >= 2) {
        // Nearest column of the biome on growing squares around the player, in steps of 8 blocks.
        const QString name = words[1].contains(u':') ? words[1] : u"qtcraft:"_s + words[1];
        const QcBlockPos feet = QcPhysics::blockContaining(client.motion.position);
        constexpr int maxRadius = 4096;
        for (int radius = 0; radius <= maxRadius; radius += 8) {
            for (int dx = -radius; dx <= radius; dx += 8) {
                for (int dz = -radius; dz <= radius; dz += 8) {
                    if (std::max(std::abs(dx), std::abs(dz)) == radius && m_world.generator().biomeAt(feet.x + dx, feet.z + dz) == name) {
                        reply(u"%1 at %2 %3"_s.arg(name).arg(feet.x + dx).arg(feet.z + dz));
                        return;
                    }
                }
            }
        }
        reply(u"No %1 within %2 blocks"_s.arg(name).arg(maxRadius));
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
    playSound(QString::fromLatin1(client.health <= 0.0 ? QcSoundKeys::playerDeath : QcSoundKeys::playerHurt),
              client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0});
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
    // airborne tick; water and ladders break a fall.
    if (client.motion.inWater || wasInWater || client.motion.climbing) {
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
    // Hits during the short invulnerability after a hit do nothing, and make no sound.
    const bool vulnerable = mob->hurtTimer <= 0.0;
    const std::optional<QList<QcItemDrop>> drops = m_mobs.damage(entityId, damage, client.motion.position);
    if (drops) {
        m_died.insert(entityId);
        playSound(definition.deathSound, lootPosition);
    } else if (vulnerable) {
        playSound(definition.hurtSound, lootPosition);
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
                if (change < 0.0) {
                    playSound(QString::fromLatin1(QcSoundKeys::playerHurt),
                              client->motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0});
                }
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
    for (const QcMobManager::Mob &mob : m_mobs.mobs()) {
        const QcMobDefinition &definition = m_content->mobs()[mob.type];
        if (!definition.ambientSound.isEmpty() && m_soundRandom.generateDouble() < dt / mobAmbientSeconds) {
            playSound(definition.ambientSound, mob.motion.position + QcVec3{0.0, definition.height * 0.8, 0.0});
        }
    }
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
    for (const QcFallingBlockManager::Falling &falling : m_falling.all()) {
        QcMsg::EntitySpawn spawn{falling.id, QcMsg::EntitySpawn::fallingBlockType, QString(), falling.motion.position, 0.0f, 0.0,
                                 {}, {}, 0.0f, 0, qcNoItem};
        spawn.block = falling.block;
        consider(spawn, false);
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
