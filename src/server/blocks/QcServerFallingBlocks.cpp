#include "QcServerFallingBlocks.h"

#include "QcBlockUpdateQueue.h"
#include "QcPhysics.h"
#include "QcServerBlocks.h"
#include "QcServerSounds.h"
#include "QcServerState.h"

QcServerFallingBlocks::QcServerFallingBlocks(QcServerState &state, QcServerSounds &sounds, QcBlockUpdateQueue &updates, QcServerBlocks &blocks)
    : m_state(state)
    , m_sounds(sounds)
    , m_updates(updates)
    , m_blocks(blocks)
{
}

void QcServerFallingBlocks::start(QcBlockPos pos, QcBlockId block)
{
    if (!m_state.entities().fallingBlocks().isFull()) {
        m_blocks.setAndBroadcast(pos, qcAirBlockId);
        m_state.entities().fallingBlocks().start(pos, block, m_state.entities().newId());
        return;
    }

    // Too many fall already. Without a resting cell (the void below, an unloaded chunk) the block
    // waits for room instead of vanishing, so a collapse of a whole bottom layer goes on entity by
    // entity within the manager's limit.
    const std::optional<QcBlockPos> rest = QcFallingBlockManager::restingCell(m_state.world(), pos);
    if (!rest) {
        m_waiting.insert(pos);
        return;
    }

    m_blocks.setAndBroadcast(pos, qcAirBlockId);

    land(block, *rest, true);
}

void QcServerFallingBlocks::retryWaiting()
{
    if (m_waiting.isEmpty() || m_state.entities().fallingBlocks().isFull()) {
        return;
    }

    // As a cascade: behind the updates of player actions.
    m_updates.setCommandMode(true);
    for (const QcBlockPos pos : std::as_const(m_waiting)) {
        m_updates.queue(pos);
    }
    m_updates.setCommandMode(false);
    m_waiting.clear();
}

void QcServerFallingBlocks::land(QcBlockId block, QcBlockPos cell, bool placed)
{
    const QcBlockDefinition &definition = m_state.content().block(block);

    // A block landing inside a player or mob would trap it, so it drops as items instead.
    QList<QcAabb> boxes;
    const QcVec3 corner{double(cell.x), double(cell.y), double(cell.z)};
    for (const QcAabb &box : m_state.content().state(block).collision) {
        boxes.append(box.moved(corner));
    }

    if (placed && QcFallingBlockManager::isPassable(m_state.world(), cell) && !m_state.entities().overlapsLiving(boxes)) {
        // Concrete powder landing in water hardens there (decision E-208).
        const std::optional<QcBlockId> replaced = m_state.world().blockAt(cell);
        const bool hardens = replaced && !definition.hardensWith.isEmpty() && m_state.content().isValidBlock(*replaced)
                             && m_state.content().block(*replaced).name == definition.hardensWith;
        const QcBlockId landed = hardens ? m_state.content().blockId(definition.hardensInto).value_or(block) : block;
        m_blocks.setAndBroadcast(cell, landed);
        if (m_state.world().blockAt(cell) == landed) {
            m_sounds.playBlock(definition, QcBlockSoundAction::Place, cell);
            return;
        }
    }

    m_blocks.dropBlockItems(block, cell);
}

void QcServerFallingBlocks::tick()
{
    if (m_state.entities().fallingBlocks().all().isEmpty()) {
        return;
    }

    const QList<QcFallingBlockManager::Landing> landed = m_state.entities().fallingBlocks().tick(m_state.world(), QcPhysics::tickDuration);

    for (const QcFallingBlockManager::Landing &landing : landed) {
        land(landing.block, landing.pos, landing.placed);
    }
}
