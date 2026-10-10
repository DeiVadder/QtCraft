#include "QcBlockSimulation.h"

#include "QcBlockRules.h"
#include "QcBlockUpdateQueue.h"
#include "QcPhysics.h"
#include "QcServerBlocks.h"
#include "QcServerSounds.h"
#include "QcServerState.h"
#include "QcSoundKeys.h"

QcBlockSimulation::QcBlockSimulation(QcServerState &state, QcServerSounds &sounds, QcBlockUpdateQueue &updates,
                                     QcServerBlocks &blocks)
    : m_state(state)
    , m_sounds(sounds)
    , m_updates(updates)
    , m_blocks(blocks)
{
}

void QcBlockSimulation::processBlockUpdates()
{
    for (int budget = maxBlockUpdatesPerTick; budget > 0; --budget) {
        const std::optional<QcBlockUpdateQueue::Update> update = m_updates.takeNext();
        if (!update) {
            break;
        }

        const QcBlockPos pos = update->pos;
        const std::optional<QcBlockId> block = m_state.world.blockAt(pos);
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
    const QcContentRegistry &content = *m_state.content;
    // Concrete powder hardens in water before it could fall on (decision E-208).
    if (const std::optional<QcBlockId> hard = QcBlockRules::hardened(m_state.world, pos, block)) {
        m_blocks.setAndBroadcast(pos, *hard);
    } else if (QcFallingBlockManager::shouldFall(m_state.world, pos)) {
        startFalling(pos, block);
    } else if (QcLiquidFlow::flows(content, block)) {
        // Lava meeting water hardens at once; flowing waits for the liquid's next step.
        if (const std::optional<QcBlockId> turned = QcLiquidFlow::reaction(m_state.world, pos, block)) {
            m_blocks.setAndBroadcast(pos, *turned);
            m_sounds.play(QString::fromLatin1(QcSoundKeys::liquidHiss), {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5});
        } else {
            m_state.liquids.schedule(m_state.world, pos, m_state.tickCount);
        }
    } else if (!QcBlockRules::isSupported(m_state.world, pos, block)) {
        // A door half without its partner was cut off by a command or a removed mod: the item
        // went with the other half.
        const bool lone = content.block(block).door && !QcBlockRules::hasDoorPartner(m_state.world, pos, block);
        m_blocks.breakBlock(pos, !lone);
    } else if (const QcBlockId connected = QcBlockRules::connectedState(m_state.world, pos, block); connected != block) {
        m_blocks.setAndBroadcast(pos, connected);
    }
}

void QcBlockSimulation::processLiquids()
{
    if (!m_state.liquids.hasDueSteps(m_state.tickCount)) {
        return;
    }

    m_liquidSoundPlayed = false;
    // Flood cascades queue behind the updates of player actions, like commands.
    m_updates.setCommandMode(true);
    const QcLiquidFlow::TickResult result = m_state.liquids.tick(
        m_state.tickCount, m_state.world, [this](QcBlockPos pos, QcBlockId block) { applyFlowStep(pos, block); });
    m_updates.setCommandMode(false);

    for (const QcBlockPos &pos : result.unloaded) {
        m_updates.waitForChunk(pos.chunk(), pos);
    }
}

void QcBlockSimulation::applyFlowStep(QcBlockPos pos, QcBlockId block)
{
    const QcContentRegistry &content = *m_state.content;
    const std::optional<QcBlockId> existing = m_state.world.blockAt(pos);
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
               && m_state.soundRandom.generateDouble() < 0.25) {
        m_liquidSoundPlayed = true;
        m_sounds.play(after.flow.sound, centre);
    }
}

void QcBlockSimulation::startFalling(QcBlockPos pos, QcBlockId block)
{
    if (!m_state.falling.isFull()) {
        m_blocks.setAndBroadcast(pos, qcAirBlockId);
        m_state.falling.start(pos, block, m_state.newEntityId());
        return;
    }

    const std::optional<QcBlockPos> rest = QcFallingBlockManager::restingCell(m_state.world, pos);
    m_blocks.setAndBroadcast(pos, qcAirBlockId);

    if (rest) {
        landBlock(block, *rest, true);
    }
}

void QcBlockSimulation::landBlock(QcBlockId block, QcBlockPos cell, bool placed)
{
    const QcBlockDefinition &definition = m_state.content->block(block);

    // A block landing inside a player or mob would trap it, so it drops as items instead.
    QList<QcAabb> boxes;
    const QcVec3 corner{double(cell.x), double(cell.y), double(cell.z)};
    for (const QcAabb &box : m_state.content->state(block).collision) {
        boxes.append(box.moved(corner));
    }

    if (placed && QcFallingBlockManager::isPassable(m_state.world, cell) && !m_blocks.overlapsBody(boxes)) {
        // Concrete powder landing in water hardens there (decision E-208).
        const std::optional<QcBlockId> replaced = m_state.world.blockAt(cell);
        const bool hardens = replaced && !definition.hardensWith.isEmpty() && m_state.content->isValidBlock(*replaced)
                             && m_state.content->block(*replaced).name == definition.hardensWith;
        const QcBlockId landed = hardens ? m_state.content->blockId(definition.hardensInto).value_or(block) : block;
        m_blocks.setAndBroadcast(cell, landed);
        if (m_state.world.blockAt(cell) == landed) {
            m_sounds.playBlock(definition, QcBlockSoundAction::Place, cell);
            return;
        }
    }

    m_blocks.dropBlockItems(definition, cell);
}

void QcBlockSimulation::tickFallingBlocks()
{
    if (m_state.falling.all().isEmpty()) {
        return;
    }

    const QList<QcFallingBlockManager::Landing> landed =
        m_state.falling.tick(m_state.world, QcPhysics::tickDuration,
                             [this](const QcVec3 &position) { return m_state.isLoadedAt(QcPhysics::blockContaining(position)); });

    for (const QcFallingBlockManager::Landing &landing : landed) {
        landBlock(landing.block, landing.pos, landing.placed);
    }
}
