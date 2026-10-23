#include "server/blocks/QcServerBlocks.h"

#include "common/blockrules/QcBlockRules.h"
#include "common/blockrules/QcRails.h"
#include "server/blocks/QcBlockUpdateQueue.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/items/QcServerExperience.h"
#include "server/mobs/QcInfestation.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcServerBlocks::QcServerBlocks(QcServerState &state, QcServerSounds &sounds, QcBlockUpdateQueue &updates)
    : m_state(state)
    , m_sounds(sounds)
    , m_updates(updates)
{
}

void QcServerBlocks::setAndBroadcast(QcBlockPos pos, QcBlockId block)
{
    if (setQuietly(pos, block)) {
        sendBlock(pos, block);
    }
}

bool QcServerBlocks::setQuietly(QcBlockPos pos, QcBlockId block)
{
    const QcBlockId before = m_state.world().blockAt(pos).value_or(qcAirBlockId);
    if (!m_state.world().setBlock(pos, block)) {
        return false;
    }

    // A machine replaced by anything but another machine spills its contents, so does a chest
    // replaced by anything but another storage block; a storage block of another size keeps what
    // fits and spills the rest (QcBlockEntities::placed). A sapling placed starts to grow.
    dropSpills();
    if (m_state.blockEntities().isSapling(block) && !m_state.content().isSameBlock(before, block)) {
        m_state.plants().saplingPlaced(m_state.blockEntities(), pos, block);
    }

    m_updates.queueNeighbours(pos, block != qcAirBlockId);
    // Frames and paintings on or in front of it check their wall and room (decision E-790).
    m_state.entities().hangings().noteBlockChange(pos);
    // Rails one block higher or lower beside a rail connect across the slope, where that rail's own
    // neighbours let it meet them (decision E-382).
    if (QcRails::isRail(m_state.content(), before) || QcRails::isRail(m_state.content(), block)) {
        for (const QcBlockPos near : QcRails::shapeNeighbours(pos)) {
            m_updates.queue(near);
        }
    }
    // Circuits nearby look at the change in the next signal step (decision E-335).
    m_state.circuits().noteChange(m_state.world(), pos, before, block);
    return true;
}

void QcServerBlocks::sendBlock(QcBlockPos pos, QcBlockId block) const
{
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing && client->view.sentChunks.contains(pos.chunk())) {
            client->connection->send(QcMsg::BlockUpdate{pos, block});
        }
    }
}

void QcServerBlocks::dropSpills()
{
    const QList<QcBlockEntities::Spill> spills = m_state.blockEntities().takeSpills();
    for (const QcBlockEntities::Spill &spill : spills) {
        dropStacks(spill.stacks, spill.pos);
        QcServerExperience::drop(m_state, QcServerExperience::takeWhole(m_state, spill.experience),
                                 {spill.pos.x + 0.5, spill.pos.y + 0.5, spill.pos.z + 0.5});
    }
    // The items of campfires that went out or were broken (decision E-772).
    const QList<QcCampfires::Spill> food = m_state.world().campfires().takeSpills();
    for (const QcCampfires::Spill &spill : food) {
        dropStacks(spill.stacks, spill.pos);
    }
}

void QcServerBlocks::breakBlock(QcBlockPos pos, bool drops)
{
    const std::optional<QcBlockId> previous = m_state.world().blockAt(pos);
    if (!previous || *previous == qcAirBlockId) {
        return;
    }

    const QcContentRegistry &content = m_state.content();
    const QcBlockDefinition &definition = content.block(*previous);
    // A slab broken out of a double slab drops both halves, a vertical one too (decision E-755).
    const bool doubleSlab = content.property(*previous, u"type"_s) == u"double"_s || content.property(*previous, u"double"_s) == u"true"_s;
    const bool paired = (definition.door || definition.bed) && QcBlockRules::hasPartner(m_state.world(), pos, *previous);
    // A portable container with contents drops as its item carrying them (QcBlockEntities spills
    // it, decision E-832), in creative mode too; empty, it drops as usual.
    const bool carriesContents = m_state.blockEntities().carriesContents(pos, *previous);

    setAndBroadcast(pos, qcAirBlockId);
    if (m_state.world().blockAt(pos) != qcAirBlockId) {
        return;
    }

    if (paired) {
        breakBlock(QcBlockRules::otherHalf(m_state.world(), pos, *previous), drops);
    }

    m_sounds.playBlock(definition, QcBlockSoundAction::Break, pos);

    // The upper half of a door and the head of a bed drop nothing: the other half carries the item.
    const bool secondHalf = (definition.door && content.property(*previous, u"half"_s) == u"top"_s)
                            || (definition.bed && content.property(*previous, u"part"_s) == u"head"_s);
    if (drops && !secondHalf && !carriesContents) {
        dropBlockItems(*previous, pos);
        if (doubleSlab) {
            dropBlockItems(*previous, pos);
        }
        // An infested block lets its creature out (decision E-453).
        QcInfestation::release(m_state, definition, pos);
    }

    m_state.plants().blockRemoved(pos, *previous, m_state.world());
}

void QcServerBlocks::dropBlockItems(QcBlockId block, QcBlockPos pos)
{
    dropItemsWithoutExperience(block, pos);

    // Ores give experience with their drops (decision E-398).
    const QcVec3 centre{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
    QcServerExperience::drop(m_state, QcServerExperience::roll(m_state, m_state.content().block(block).experience), centre);
}

void QcServerBlocks::dropItemsWithoutExperience(QcBlockId block, QcBlockPos pos)
{
    const QcContentRegistry &content = m_state.content();
    const QcVec3 centre{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
    for (const QcItemDrop &drop : content.block(block).drops) {
        // Crops give their harvest only when ripe (decision E-289).
        const bool applies = std::ranges::all_of(drop.when, [&](const QcShapeCondition &condition) {
            return condition.values.contains(content.property(block, condition.property));
        });
        if (!applies || (drop.chance < 1.0 && m_state.random().generateDouble() >= drop.chance)) {
            continue;
        }
        if (const std::optional<QcItemId> item = m_state.content().itemId(drop.item)) {
            const int extra = drop.mostCount() > drop.count
                ? static_cast<int>(m_state.random().bounded(static_cast<quint32>(drop.mostCount() - drop.count + 1))) : 0;
            m_state.entities().items().spawnScattered({*item, drop.count + extra, 0}, centre, m_state.entities().newId());
        }
    }
}

void QcServerBlocks::dropStacks(const QList<QcItemStack> &stacks, QcBlockPos pos)
{
    const QcVec3 centre{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
    for (const QcItemStack &stack : stacks) {
        if (!stack.isEmpty()) {
            m_state.entities().items().spawnScattered(stack, centre, m_state.entities().newId());
        }
    }
}
