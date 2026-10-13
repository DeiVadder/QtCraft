#include "QcServerMachines.h"

#include "QcChatText.h"
#include "QcPhysics.h"
#include "QcServerBlocks.h"
#include "QcServerSounds.h"
#include "QcServerState.h"

using namespace Qt::StringLiterals;

QcServerMachines::QcServerMachines(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks)
    : m_state(state)
    , m_sounds(sounds)
    , m_blocks(blocks)
{
}

QcMachineManager::Machine *QcServerMachines::openMachineOf(const QcServerClient &client)
{
    if (!client.machine) {
        return nullptr;
    }

    const std::optional<QcBlockId> block = m_state.world.blockAt(*client.machine);
    return block && m_state.machines.machineOf(*block) ? m_state.machines.find(*client.machine) : nullptr;
}

QcContainerManager::Container *QcServerMachines::openContainerOf(const QcServerClient &client)
{
    if (!client.machine) {
        return nullptr;
    }

    const std::optional<QcBlockId> block = m_state.world.blockAt(*client.machine);
    return block && m_state.containers.slotsOf(*block) > 0 ? m_state.containers.find(*client.machine) : nullptr;
}

void QcServerMachines::open(QcServerClient &client, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_state.world.blockAt(pos);
    if (const int slotCount = block ? m_state.containers.slotsOf(*block) : 0; slotCount > 0) {
        if (!m_state.containers.add(pos, slotCount)) {
            client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Too many chests in the world (at most %1)"),
                {QString::number(QcContainerManager::maxContainers)}));
            return;
        }

        client.machine = pos;
        client.sentMachine.reset();
        client.connection->send(QcMsg::OpenMachine{pos, {}});
        send(client, true);
        return;
    }

    const QcMachineDefinition *definition = block ? m_state.machines.machineOf(*block) : nullptr;
    if (!definition) {
        return;
    }

    if (!m_state.machines.add(pos)) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Too many machines in the world (at most %1)"),
            {QString::number(QcMachineManager::maxMachines)}));
        return;
    }

    client.machine = pos;
    client.sentMachine.reset();
    client.connection->send(QcMsg::OpenMachine{pos, definition->category});
    send(client, true);
}

void QcServerMachines::close(QcServerClient &client, bool notify)
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

void QcServerMachines::send(QcServerClient &client, bool force)
{
    if (const QcContainerManager::Container *container = openContainerOf(client)) {
        sendContainer(client, *container, force);
        return;
    }

    const QcMachineManager::Machine *machine = openMachineOf(client);
    if (!machine) {
        return;
    }

    QcMsg::MachineState state;
    state.pos = machine->pos;
    state.stacks = QList<QcItemStack>(machine->stacks.begin(), machine->stacks.end());
    state.burn = float(m_state.machines.burnFraction(*machine));
    state.progress = float(m_state.machines.progressFraction(*machine, m_state.world.blockAt(machine->pos).value_or(qcAirBlockId)));

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

void QcServerMachines::sendContainer(QcServerClient &client, const QcContainerManager::Container &container, bool force)
{
    QcMsg::MachineState state;
    state.pos = container.pos;
    state.stacks = container.stacks;

    if (force || !client.sentMachine || client.sentMachine->stacks != state.stacks) {
        client.sentMachine = state;
        client.connection->send(state);
    }
}

void QcServerMachines::sendToViewers(QcBlockPos pos)
{
    for (const auto &other : m_state.clients) {
        if (other->machine == pos) {
            send(*other, true);
        }
    }
}

void QcServerMachines::tick()
{
    QList<QcBlockPos> slotsChanged;
    const QList<QcMachineManager::Machine> removed = m_state.machines.tick(m_state.world, QcPhysics::tickDuration, nullptr, &slotsChanged);
    for (const QcBlockPos &pos : std::as_const(slotsChanged)) {
        m_state.noteContents(pos);
    }
    for (const QcMachineManager::Machine &machine : removed) {
        m_blocks.dropMachineContents(machine);
    }

    playMachineSounds();

    for (const auto &client : m_state.clients) {
        if (client->state != QcServerClient::State::Playing || !client->machine) {
            continue;
        }

        // The screen closes when the machine is gone, its chunk unloaded or the player walked away.
        if (client->dead || (!openMachineOf(*client) && !openContainerOf(*client)) || !m_state.isWithinReach(*client, *client->machine)) {
            close(*client, true);
        } else {
            send(*client);
        }
    }
}

void QcServerMachines::playMachineSounds()
{
    for (const QcMachineManager::Machine &machine : m_state.machines.all()) {
        if (machine.burnRemaining <= 0.0 || !m_state.isLoadedAt(machine.pos)
            || m_state.soundRandom.generateDouble() >= QcPhysics::tickDuration / machineSoundSeconds) {
            continue;
        }

        const std::optional<QcBlockId> block = m_state.world.blockAt(machine.pos);
        if (const QcMachineDefinition *definition = block ? m_state.machines.machineOf(*block) : nullptr) {
            m_sounds.play(definition->sound, {machine.pos.x + 0.5, machine.pos.y + 0.5, machine.pos.z + 0.5});
        }
    }
}
