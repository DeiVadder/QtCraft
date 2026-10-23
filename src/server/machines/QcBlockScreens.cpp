#include "server/machines/QcBlockScreens.h"

#include "common/gameplay/QcPortableContainers.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"

using namespace Qt::StringLiterals;

namespace {

// Whether `block` is a container that counts its viewers.
bool countsViewers(const QcServerState &state, QcBlockId block)
{
    return state.content().isValidBlock(block) && state.content().block(block).signal.kind == QcSignalKind::Viewers;
}

} // namespace

QcBlockScreens::QcBlockScreens(QcServerState &state, QcServerBlocks &blocks)
    : m_state(state)
    , m_blocks(blocks)
{
}

QcBlockEntities::Machine *QcBlockScreens::openMachineOf(const QcServerClient &client)
{
    if (!client.screen.open) {
        return nullptr;
    }

    const std::optional<QcBlockId> block = m_state.world().blockAt(*client.screen.open);
    return block && m_state.machineRules().machineOf(*block) ? m_state.blockEntities().machine(*client.screen.open) : nullptr;
}

std::optional<QcBlockScreens::OpenContainer> QcBlockScreens::openContainerOf(QcServerClient &client)
{
    if (client.screen.held) {
        return heldContainerOf(client);
    }
    if (client.screen.cart != 0) {
        QcMinecart *cart = m_state.entities().minecarts().find(client.screen.cart);
        if (!cart || cart->stacks.isEmpty()) {
            return std::nullopt;
        }
        return OpenContainer{&cart->stacks, {}, cart->id};
    }
    if (!client.screen.open) {
        return std::nullopt;
    }

    const std::optional<QcBlockId> block = m_state.world().blockAt(*client.screen.open);
    QcBlockEntities::Container *container = block && m_state.blockEntities().containerSlots(*block) > 0
                                                ? m_state.blockEntities().container(*client.screen.open)
                                                : nullptr;
    if (!container) {
        return std::nullopt;
    }
    // A container exists only for a block that is there.
    const bool portable = m_state.blockEntities().isPortable(block.value_or(qcAirBlockId));
    return OpenContainer{&container->stacks, container->pos, 0, portable, QcMsg::OpenMachine::noHeldSlot};
}

std::optional<QcBlockScreens::OpenContainer> QcBlockScreens::heldContainerOf(QcServerClient &client)
{
    // The item in the locked slot is the truth; the working copy follows it.
    if (!client.screen.held) {
        return std::nullopt;
    }
    QcServerClient::HeldContainer &held = *client.screen.held;
    const QcItemStack &stack = client.inventory.slot(held.slot);
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> block =
        QcPortableContainers::isPortable(content, stack.item) ? content.blockId(content.item(stack.item).placesBlock) : std::nullopt;
    const int slotCount = block ? content.block(*block).containerSlots : 0;
    if (stack.isEmpty() || slotCount <= 0) {
        return std::nullopt;
    }
    held.stacks = QcPortableContainers::slotsOf(content, stack, slotCount);
    return OpenContainer{&held.stacks, {}, 0, true, held.slot};
}

std::optional<QList<QcItemStack>> QcBlockScreens::openHeld(QcServerClient &client)
{
    const int slot = client.inventory.selectedHotbarSlot();
    const QcItemStack stack = client.inventory.slot(slot);
    if (client.dead || !QcPortableContainers::isPortable(m_state.content(), stack.item)) {
        return std::nullopt;
    }

    closeOtherScreens(client);
    client.screen.open.reset();
    client.screen.cart = 0;
    client.screen.held = QcServerClient::HeldContainer{slot, {}};
    const std::optional<OpenContainer> container = heldContainerOf(client);
    if (!container) {
        client.screen.held.reset();
        return std::nullopt;
    }

    // The item keeps only what its slots show; the rest goes back to the player.
    QList<QcItemStack> leftOver = QcPortableContainers::leftOver(m_state.content(), stack, int(container->stacks->size()));
    if (!leftOver.isEmpty()) {
        const std::optional<QcItemStack> trimmed = stack.tryWithContents(*container->stacks);
        if (!trimmed) {
            qCWarning(lcServer) << "A held backpack whose contents cannot be trimmed to its slots stays closed, slot" << slot;
            client.screen.held.reset();
            return std::nullopt;
        }
        client.inventory.setSlot(slot, *trimmed);
    }

    client.screen.sent.reset();
    QcMsg::OpenMachine open;
    open.heldSlot = slot;
    client.connection->send(open);
    send(client, true);
    return leftOver;
}

bool QcBlockScreens::hasScreen(const QcServerClient &client)
{
    return client.screen.open || client.screen.cart != 0 || client.screen.held;
}

void QcBlockScreens::openCart(QcServerClient &client, const QcMinecart &cart)
{
    if (cart.stacks.isEmpty()) {
        return;
    }
    closeOtherScreens(client);
    client.screen.open.reset();
    client.screen.held.reset();
    client.screen.cart = cart.id;
    client.screen.sent.reset();
    client.connection->send(QcMsg::OpenMachine{QcPhysics::blockContaining(cart.motion.position), {}, cart.id});
    send(client, true);
}

void QcBlockScreens::containerChanged(QcServerClient &client, const OpenContainer &container, bool changed)
{
    if (container.heldSlot >= 0) {
        send(client, true);
        return;
    }
    if (container.cart == 0) {
        sendToViewers(container.pos);
        if (changed) {
            m_state.noteContents(container.pos);
        }
        return;
    }

    sendToCartViewers(container.cart);
    const QcMinecart *cart = m_state.entities().minecarts().find(container.cart);
    if (changed && cart) {
        // A comparator reading the detector rail under the cart (decision E-385).
        m_state.noteContents(QcPhysics::blockContaining(cart->motion.position));
    }
}

void QcBlockScreens::sendToCartViewers(quint32 cart)
{
    for (const auto &other : m_state.clients()) {
        if (other->screen.cart == cart) {
            send(*other, true);
        }
    }
}

void QcBlockScreens::open(QcServerClient &client, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!block) {
        return;
    }
    QcBlockEntities &entities = m_state.blockEntities();
    if (entities.containerSlots(*block) > 0) {
        // The block's container exists unless the table was full when it was placed.
        if (!entities.ensure(pos, *block)) {
            client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Too many chests in the world (at most %1)"),
                {QString::number(entities.limits().containers)}));
            return;
        }

        closeOtherScreens(client);
        client.screen.held.reset();
        client.screen.open = pos;
        client.screen.sent.reset();
        client.connection->send(QcMsg::OpenMachine{pos, {}});
        send(client, true);
        return;
    }

    const QcMachineDefinition *definition = m_state.machineRules().machineOf(*block);
    if (!definition) {
        return;
    }

    if (!entities.ensure(pos, *block)) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Too many machines in the world (at most %1)"),
            {QString::number(entities.limits().machines)}));
        return;
    }

    closeOtherScreens(client);
    client.screen.held.reset();
    client.screen.open = pos;
    client.screen.sent.reset();
    client.connection->send(QcMsg::OpenMachine{pos, definition->category});
    send(client, true);
}

void QcBlockScreens::setStonecutterCloser(std::function<void(QcServerClient &)> closer)
{
    m_closeStonecutter = std::move(closer);
}

void QcBlockScreens::setEnchantingCloser(std::function<void(QcServerClient &)> closer)
{
    m_closeEnchanting = std::move(closer);
}

void QcBlockScreens::closeOtherScreens(QcServerClient &client)
{
    closeStonecutter(client);
    closeEnchanting(client);
    endTrade(client);
}

void QcBlockScreens::closeStonecutter(QcServerClient &client)
{
    // The stonecutter's input goes back to the player (QcServerStonecutter::close).
    if (client.screen.stonecutter && m_closeStonecutter) {
        m_closeStonecutter(client);
    }
}

void QcBlockScreens::closeEnchanting(QcServerClient &client)
{
    // The table's item and lapis go back to the player (QcServerEnchanting::close).
    if (client.screen.enchanting && m_closeEnchanting) {
        m_closeEnchanting(client);
    }
}

void QcBlockScreens::endTrade(QcServerClient &client)
{
    if (client.screen.trading == 0) {
        return;
    }

    // QcServerTrading::update releases the villager, whose trader no longer trades with it.
    client.screen.trading = 0;
    client.screen.sentTrade.reset();
    client.connection->send(QcMsg::CloseTrade{});
}

void QcBlockScreens::close(QcServerClient &client, bool notify)
{
    if (!hasScreen(client)) {
        return;
    }

    client.screen.open.reset();
    client.screen.cart = 0;
    client.screen.held.reset();
    client.screen.sent.reset();

    if (notify) {
        client.connection->send(QcMsg::CloseMachine{});
    }
}

void QcBlockScreens::send(QcServerClient &client, bool force)
{
    if (const std::optional<OpenContainer> container = openContainerOf(client)) {
        sendContainer(client, *container, force);
        return;
    }

    const QcBlockEntities::Machine *machine = openMachineOf(client);
    if (!machine) {
        return;
    }

    QcMsg::MachineState state;
    state.pos = machine->pos;
    state.stacks = QList<QcItemStack>(machine->stacks.begin(), machine->stacks.end());
    state.burn = float(m_state.machineRules().burnFraction(*machine));
    state.progress = float(m_state.machineRules().progressFraction(*machine, m_state.world().blockAt(machine->pos).value_or(qcAirBlockId)));

    // Bars move in steps of one percent; smaller changes are not worth a message.
    const auto percent = [](float value) { return qRound(value * 100.0f); };
    const std::optional<QcMsg::MachineState> &sent = client.screen.sent;
    if (!force && sent && sent->stacks == state.stacks && percent(sent->burn) == percent(state.burn)
        && percent(sent->progress) == percent(state.progress)) {
        return;
    }

    client.screen.sent = state;
    client.connection->send(state);
}

void QcBlockScreens::sendContainer(QcServerClient &client, const OpenContainer &container, bool force)
{
    QcMsg::MachineState state;
    // The client knows a cart's screen by its entity.
    state.pos = container.pos;
    state.entity = container.cart;
    state.stacks = *container.stacks;

    if (force || !client.screen.sent || client.screen.sent->stacks != state.stacks) {
        client.screen.sent = state;
        client.connection->send(state);
    }
}

void QcBlockScreens::sendToViewers(QcBlockPos pos)
{
    for (const auto &other : m_state.clients()) {
        if (other->screen.cart == 0 && other->screen.open == pos) {
            send(*other, true);
        }
    }
}

void QcBlockScreens::update()
{
    for (const auto &client : m_state.clients()) {
        if (client->state != QcServerClient::State::Playing || !hasScreen(*client)) {
            continue;
        }

        // The screen closes when the machine or cart is gone, its chunk unloaded or the player walked away.
        if (client->dead || (!openMachineOf(*client) && !openContainerOf(*client)) || !canReach(*client)) {
            close(*client, true);
        } else {
            send(*client);
        }
    }
    updateViewers();
}

void QcBlockScreens::updateViewers()
{
    QHash<QcBlockPos, int> viewers;
    for (const auto &client : m_state.clients()) {
        const std::optional<QcBlockPos> open = client->screen.cart == 0 ? client->screen.open : std::nullopt;
        if (!open || client->state != QcServerClient::State::Playing || client->dead) {
            continue;
        }
        if (countsViewers(m_state, m_state.world().blockAt(*open).value_or(qcAirBlockId))) {
            ++viewers[*open];
        }
    }

    // Those looked into now and those nobody looks into any more.
    QHash<QcBlockPos, int> changed = m_viewers;
    changed.insert(viewers);
    for (auto it = changed.cbegin(); it != changed.cend(); ++it) {
        const std::optional<QcBlockId> block = m_state.world().blockAt(it.key());
        const QString power = QString::number(std::min(viewers.value(it.key()), QcSignalDefinition::maxPower));
        if (block && countsViewers(m_state, *block) && m_state.content().property(*block, u"power"_s) != power) {
            m_blocks.setAndBroadcast(it.key(), m_state.content().withProperty(*block, u"power"_s, power));
        }
    }
    m_viewers = std::move(viewers);
}

void QcBlockScreens::releaseViewers(QcServerState &state, QcChunkPos chunk)
{
    const QcBlockEntities::ChunkIndex *index = state.blockEntities().chunkIndex(chunk);
    if (!index) {
        return;
    }
    // A copy: the index may change with the blocks.
    const QList<QcBlockPos> containers = index->containers;
    for (const QcBlockPos &pos : containers) {
        const QcBlockId block = state.world().blockAt(pos).value_or(qcAirBlockId);
        if (countsViewers(state, block) && state.content().property(block, u"power"_s) != u"0"_s) {
            const QcBlockId released = state.content().withProperty(block, u"power"_s, u"0"_s);
            state.world().setBlock(pos, released);
            state.circuits().noteChange(state.world(), pos, block, released);
        }
    }
}

bool QcBlockScreens::canReach(const QcServerClient &client) const
{
    // A held backpack is always at hand.
    if (client.screen.held) {
        return true;
    }
    if (client.screen.cart == 0) {
        return client.screen.open && QcLineOfSight::isWithinReach(client, *client.screen.open);
    }
    const QcMinecart *cart = m_state.entities().minecarts().find(client.screen.cart);
    return cart && QcLineOfSight::isWithinReach(client, QcPhysics::blockContaining(cart->motion.position));
}
