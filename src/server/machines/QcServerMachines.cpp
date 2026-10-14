#include "QcServerMachines.h"

#include "QcPhysics.h"
#include "QcServerSounds.h"
#include "QcServerState.h"

QcServerMachines::QcServerMachines(QcServerState &state, QcServerSounds &sounds)
    : m_state(state)
    , m_sounds(sounds)
{
}

void QcServerMachines::tick()
{
    QList<QcBlockPos> slotsChanged;
    m_state.machineRules.tick(m_state.blockEntities(), m_state.world, QcPhysics::tickDuration, nullptr, &slotsChanged);
    for (const QcBlockPos &pos : std::as_const(slotsChanged)) {
        m_state.noteContents(pos);
    }
    playMachineSounds();
}

void QcServerMachines::playMachineSounds()
{
    m_state.blockEntities().forEachMachine([this](const QcBlockEntities::Machine &machine) {
        if (machine.burnRemaining <= 0.0 || !m_state.isLoadedAt(machine.pos)
            || m_state.soundRandom.generateDouble() >= QcPhysics::tickDuration / machineSoundSeconds) {
            return;
        }

        const std::optional<QcBlockId> block = m_state.world.blockAt(machine.pos);
        if (const QcMachineDefinition *definition = block ? m_state.machineRules.machineOf(*block) : nullptr) {
            m_sounds.play(definition->sound, {machine.pos.x + 0.5, machine.pos.y + 0.5, machine.pos.z + 0.5});
        }
    });
}
