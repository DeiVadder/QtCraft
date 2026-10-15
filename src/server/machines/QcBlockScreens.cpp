#include "QcBlockScreens.h"

#include "QcChatText.h"
#include "QcLineOfSight.h"
#include "QcPhysics.h"
#include "QcServerState.h"

using namespace Qt::StringLiterals;

QcBlockScreens::QcBlockScreens(QcServerState &state)
    : m_state(state)
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

std::optional<QcBlockScreens::OpenContainer> QcBlockScreens::openContainerOf(const QcServerClient &client)
{
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
    return OpenContainer{&container->stacks, container->pos, 0};
}

void QcBlockScreens::openCart(QcServerClient &client, const QcMinecart &cart)
{
    if (cart.stacks.isEmpty()) {
        return;
    }
    client.screen.open.reset();
    client.screen.cart = cart.id;
    client.screen.sent.reset();
    client.connection->send(QcMsg::OpenMachine{QcPhysics::blockContaining(cart.motion.position), {}, cart.id});
    send(client, true);
}

void QcBlockScreens::containerChanged(const OpenContainer &container, bool changed)
{
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

    client.screen.open = pos;
    client.screen.sent.reset();
    client.connection->send(QcMsg::OpenMachine{pos, definition->category});
    send(client, true);
}

void QcBlockScreens::close(QcServerClient &client, bool notify)
{
    if (!client.screen.open && client.screen.cart == 0) {
        return;
    }

    client.screen.open.reset();
    client.screen.cart = 0;
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
        if (client->state != QcServerClient::State::Playing || (!client->screen.open && client->screen.cart == 0)) {
            continue;
        }

        // The screen closes when the machine or cart is gone, its chunk unloaded or the player walked away.
        if (client->dead || (!openMachineOf(*client) && !openContainerOf(*client)) || !canReach(*client)) {
            close(*client, true);
        } else {
            send(*client);
        }
    }
}

bool QcBlockScreens::canReach(const QcServerClient &client) const
{
    if (client.screen.cart == 0) {
        return client.screen.open && QcLineOfSight::isWithinReach(client, *client.screen.open);
    }
    const QcMinecart *cart = m_state.entities().minecarts().find(client.screen.cart);
    return cart && QcLineOfSight::isWithinReach(client, QcPhysics::blockContaining(cart->motion.position));
}
