#include "QcBlockSimulation.h"

#include "QcBlockRules.h"
#include "QcBlockUpdateQueue.h"
#include "QcFireIgnition.h"
#include "QcServerBlocks.h"
#include "QcServerFallingBlocks.h"
#include "QcServerSounds.h"
#include "QcServerState.h"
#include "QcSoundKeys.h"

QcBlockSimulation::QcBlockSimulation(QcServerState &state, QcServerSounds &sounds, QcBlockUpdateQueue &updates,
                                     QcServerBlocks &blocks, QcServerFallingBlocks &falling)
    : m_state(state)
    , m_sounds(sounds)
    , m_updates(updates)
    , m_blocks(blocks)
    , m_falling(falling)
    , m_fireTable(state.content())
{
}

void QcBlockSimulation::processBlockUpdates()
{
    m_falling.retryWaiting();

    for (qint64 budget = m_state.config().limits.blockUpdatesPerTick; budget > 0; --budget) {
        const std::optional<QcBlockUpdateQueue::Update> update = m_updates.takeNext();
        if (!update) {
            break;
        }

        const QcBlockPos pos = update->pos;
        const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
        if (!block) {
            m_updates.waitForChunk(pos.chunk(), pos);
            continue;
        }

        if (*block == qcAirBlockId) {
            continue;
        }

        // Cascades stay in the queue they come from.
        m_updates.setCommandMode(update->fromCommand);
        processUpdate(pos, *block);
        m_updates.setCommandMode(false);
    }
}

void QcBlockSimulation::processUpdate(QcBlockPos pos, QcBlockId block)
{
    const QcContentRegistry &content = m_state.content();
    // Concrete powder hardens in water before it could fall on (decision E-208).
    if (const std::optional<QcBlockId> hard = QcBlockRules::hardened(m_state.world(), pos, block)) {
        m_blocks.setAndBroadcast(pos, *hard);
    } else if (QcFallingBlockManager::shouldFall(m_state.world(), pos)) {
        m_falling.start(pos, block);
    } else if (QcLiquidFlow::flows(content, block)) {
        // Lava meeting water hardens at once; flowing waits for the liquid's next step.
        if (const std::optional<QcBlockId> turned = QcLiquidFlow::reaction(m_state.world(), pos, block)) {
            m_blocks.setAndBroadcast(pos, *turned);
            m_sounds.play(QString::fromLatin1(QcSoundKeys::liquidHiss), {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5});
        } else {
            m_state.liquids().schedule(m_state.world(), pos, m_state.clock().tick());
            if (m_ignition) {
                m_ignition->blockUpdated(pos, block);
            }
        }
    } else if (m_fireTable.of(block).fire && !QcFireRules::canStay(m_state.world(), m_fireTable, pos)) {
        // Fire whose ground or fuel went away goes out at once (decision E-392).
        m_blocks.setAndBroadcast(pos, qcAirBlockId);
    } else if (!QcBlockRules::isSupported(m_state.world(), pos, block)) {
        // A door half without its partner was cut off by a command or a removed mod: the item
        // went with the other half.
        const bool lone = content.block(block).door && !QcBlockRules::hasDoorPartner(m_state.world(), pos, block);
        m_blocks.breakBlock(pos, !lone);
    } else if (const QcBlockId connected = QcBlockRules::connectedState(m_state.world(), pos, block); connected != block) {
        m_blocks.setAndBroadcast(pos, connected);
    }
}

void QcBlockSimulation::processLiquids()
{
    if (!m_state.liquids().hasDueSteps(m_state.clock().tick())) {
        return;
    }

    m_liquidSoundPlayed = false;
    // Flood cascades queue behind the updates of player actions, like commands.
    m_updates.setCommandMode(true);
    const QcLiquidFlow::TickResult result = m_state.liquids().tick(
        m_state.clock().tick(), m_state.world(), [this](QcBlockPos pos, QcBlockId block) { applyFlowStep(pos, block); },
        int(m_state.config().limits.liquidStepsPerTick));
    m_updates.setCommandMode(false);

    for (const QcBlockPos &pos : result.unloaded) {
        m_updates.waitForChunk(pos.chunk(), pos);
    }
}

void QcBlockSimulation::applyFlowStep(QcBlockPos pos, QcBlockId block)
{
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> existing = m_state.world().blockAt(pos);
    if (!existing || !content.isValidBlock(*existing)) {
        return;
    }

    const QcBlockDefinition &before = content.block(*existing);
    const QcVec3 centre{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};

    if (*existing != qcAirBlockId && !before.liquid && before.washable) {
        m_blocks.breakBlock(pos, true);
    }

    m_blocks.setAndBroadcast(pos, block);
    const QcBlockDefinition &after = content.block(block);
    if (before.liquid && !after.liquid && block != qcAirBlockId) {
        m_sounds.play(QString::fromLatin1(QcSoundKeys::liquidHiss), centre);
    } else if (after.liquid && !after.flow.sound.isEmpty() && !m_liquidSoundPlayed && !content.isSameBlock(*existing, block)
               && m_state.soundRandom().generateDouble() < 0.25) {
        m_liquidSoundPlayed = true;
        m_sounds.play(after.flow.sound, centre);
    }
}
