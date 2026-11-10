// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/machines/QcServerMachines.h"

#include "common/physics/QcPhysics.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"

using namespace Qt::StringLiterals;

QcServerMachines::QcServerMachines(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks)
    : m_state(state)
    , m_sounds(sounds)
    , m_blocks(blocks)
{
}

void QcServerMachines::tick()
{
    const QList<QcBlockPos> slotsChanged = m_state.machineRules().tick(m_state.blockEntities(), m_state.world(), QcPhysics::tickDuration);
    for (const QcBlockPos &pos : std::as_const(slotsChanged)) {
        m_state.noteContents(pos);
    }
    const QcBrewingRules::Changes brewing = m_state.machineRules().brewing().tick(m_state.blockEntities(), m_state.world(), QcPhysics::tickDuration);
    for (const QcBlockPos &pos : brewing.slotsChanged) {
        m_state.noteContents(pos);
    }
    for (const QcBlockPos &pos : brewing.brewed) {
        playBrewedSound(pos);
    }
    showBottles();
    playMachineSounds();
}

void QcServerMachines::playBrewedSound(QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    const QcMachineDefinition *definition = block ? m_state.machineRules().machineOf(*block) : nullptr;
    if (definition && definition->brewing && !definition->brewing->finishSound.isEmpty()) {
        m_sounds.play(definition->brewing->finishSound, {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5});
    }
}

void QcServerMachines::showBottles()
{
    const QcContentRegistry &content = m_state.content();
    QList<std::pair<QcBlockPos, QcBlockId>> changed;
    m_state.blockEntities().forEachMachine([&](QcBlockEntities::Machine &machine) {
        if (machine.stacks.size() != QcBrewerDefinition::slotCount) {
            return;
        }

        // Only a change of the slots costs a look at the block.
        int filled = 0;
        for (int i = 0; i < QcBrewingRules::bottleSlots; ++i) {
            filled |= machine.stacks.at(QcBrewingRules::firstBottleSlot + i).isEmpty() ? 0 : 1 << i;
        }
        const std::optional<QcBlockId> block = filled == machine.shownBottles ? std::nullopt : m_state.world().blockAt(machine.pos);
        if (!block) {
            return;
        }

        machine.shownBottles = filled;
        QcBlockId shown = *block;
        for (int i = 0; i < QcBrewingRules::bottleSlots; ++i) {
            shown = content.withProperty(shown, u"bottle_%1"_s.arg(i), (filled >> i & 1) != 0 ? u"true"_s : u"false"_s);
        }
        if (shown != *block) {
            changed.append({machine.pos, shown});
        }
    });
    // Afterwards: setting a block may touch the table of machines.
    for (const auto &[pos, block] : std::as_const(changed)) {
        m_blocks.setAndBroadcast(pos, block);
    }
}

void QcServerMachines::playMachineSounds()
{
    m_state.blockEntities().forEachMachine([this](const QcBlockEntities::Machine &machine) {
        const bool brewing = machine.stacks.size() == QcBrewerDefinition::slotCount;
        if ((brewing ? machine.progress : machine.burnRemaining) <= 0.0 || !m_state.isLoadedAt(machine.pos)
            || m_state.soundRandom().generateDouble() >= QcPhysics::tickDuration / machineSoundSeconds) {
            return;
        }

        const std::optional<QcBlockId> block = m_state.world().blockAt(machine.pos);
        if (const QcMachineDefinition *definition = block ? m_state.machineRules().machineOf(*block) : nullptr) {
            m_sounds.play(definition->sound, {machine.pos.x + 0.5, machine.pos.y + 0.5, machine.pos.z + 0.5});
        }
    });
}
