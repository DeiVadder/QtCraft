#include "QcBlockScreens.h"

#include "QcChatText.h"
#include "QcLineOfSight.h"
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

QcBlockEntities::Container *QcBlockScreens::openContainerOf(const QcServerClient &client)
{
    if (!client.screen.open) {
        return nullptr;
    }

    const std::optional<QcBlockId> block = m_state.world().blockAt(*client.screen.open);
    return block && m_state.blockEntities().containerSlots(*block) > 0 ? m_state.blockEntities().container(*client.screen.open) : nullptr;
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
    if (!client.screen.open) {
        return;
    }

    client.screen.open.reset();
    client.screen.sent.reset();

    if (notify) {
        client.connection->send(QcMsg::CloseMachine{});
    }
}

void QcBlockScreens::send(QcServerClient &client, bool force)
{
    if (const QcBlockEntities::Container *container = openContainerOf(client)) {
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

void QcBlockScreens::sendContainer(QcServerClient &client, const QcBlockEntities::Container &container, bool force)
{
    QcMsg::MachineState state;
    state.pos = container.pos;
    state.stacks = container.stacks;

    if (force || !client.screen.sent || client.screen.sent->stacks != state.stacks) {
        client.screen.sent = state;
        client.connection->send(state);
    }
}

void QcBlockScreens::sendToViewers(QcBlockPos pos)
{
    for (const auto &other : m_state.clients()) {
        if (other->screen.open == pos) {
            send(*other, true);
        }
    }
}

void QcBlockScreens::update()
{
    for (const auto &client : m_state.clients()) {
        if (client->state != QcServerClient::State::Playing || !client->screen.open) {
            continue;
        }

        // The screen closes when the machine is gone, its chunk unloaded or the player walked away.
        if (client->dead || (!openMachineOf(*client) && !openContainerOf(*client)) || !QcLineOfSight::isWithinReach(*client, *client->screen.open)) {
            close(*client, true);
        } else {
            send(*client);
        }
    }
}
