#include "server/explosions/QcBlastBlocks.h"

#include "server/blockentities/QcBlockEntities.h"
#include "server/blocks/QcBlockRules.h"
#include "server/blocks/QcFireRules.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/blocks/QcServerFire.h"
#include "server/core/QcServerState.h"
#include "server/entities/QcEntities.h"
#include "server/explosions/QcBlastTable.h"
#include "server/mobs/QcInfestation.h"

namespace {

// What the dice decide, so that no two decisions on a cell share a number.
enum Salt : quint32 {
    DropSalt = 110,
    FireSalt = 120,
};

constexpr quint32 dropDice = 1000;
// One in this many broken cells of a blast with fire catches.
constexpr quint32 fireOneIn = 3;

} // namespace

QcBlastBlocks::QcBlastBlocks(QcServerState &state, QcServerBlocks &blocks, QcServerFire &fire, const QcBlastTable &table)
    : m_state(state)
    , m_blocks(blocks)
    , m_fire(fire)
    , m_table(table)
{
}

void QcBlastBlocks::add(const QList<QcBlockPos> &cells, const Effects &effects)
{
    for (const QcBlockPos &pos : cells) {
        m_waiting.push_back({pos, effects});
    }
}

quint32 QcBlastBlocks::roll(const Work &work, quint32 salt, quint32 bound) const
{
    const QcFireRules::Dice dice{quint64(m_state.config().seed), work.effects.centre, 0};
    return dice.roll(work.pos, salt, bound);
}

QList<QcBlastBlocks::Chained> QcBlastBlocks::take(qint64 budget)
{
    QList<Chained> chained;
    for (; budget > 0 && !m_waiting.empty(); --budget) {
        const Work work = m_waiting.front();
        m_waiting.pop_front();
        const std::optional<QcBlockId> block = m_state.world().blockAt(work.pos);
        if (!block || *block == qcAirBlockId) {
            continue;
        }
        if (m_table.of(*block).explosive) {
            chained.append({work.pos, *block});
            continue;
        }
        breakCell(work, *block);
    }
    return chained;
}

bool QcBlastBlocks::clear(QcBlockPos pos)
{
    if (!m_blocks.setQuietly(pos, qcAirBlockId)) {
        return false;
    }
    m_batch.append({pos, qcAirBlockId});
    return true;
}

void QcBlastBlocks::breakCell(const Work &work, QcBlockId block)
{
    const QcBlockDefinition &definition = m_state.content().block(block);
    // Block drops stay away while many drops wait for room, so the contents chests spill (which
    // always drop) are not pushed out of the waiting queue by rubble.
    const bool drops = double(roll(work, DropSalt, dropDice)) < work.effects.dropChance * dropDice
                       && !m_state.entities().items().crowded();
    // A door or bed breaks as a whole, with its sound, like a player breaks it.
    if ((definition.door || definition.bed) && QcBlockRules::hasPartner(m_state.world(), work.pos, block)) {
        m_blocks.breakBlock(work.pos, drops);
        ++m_broken;
        return;
    }
    // A portable container with contents leaves as its item carrying them (QcBlockEntities spills
    // it when the cell clears, decision E-832) instead of its usual drop.
    const bool carriesContents = m_state.blockEntities().carriesContents(work.pos, block);
    if (!clear(work.pos)) {
        return;
    }

    ++m_broken;
    m_state.plants().blockRemoved(work.pos, block, m_state.world());
    if (drops && !carriesContents) {
        m_blocks.dropItemsWithoutExperience(block, work.pos);
    }
    // An infested block lets its creature out (decision E-453).
    QcInfestation::release(m_state, definition, work.pos);
    if (work.effects.fire && roll(work, FireSalt, fireOneIn) == 0) {
        m_fires.append(work.pos);
    }
}

void QcBlastBlocks::flush()
{
    sendBatch();
    // Fires after the air they stand in reached the clients.
    for (const QcBlockPos &pos : std::as_const(m_fires)) {
        m_fire.ignite(pos);
    }
    m_fires.clear();
}

void QcBlastBlocks::sendBatch()
{
    if (m_batch.isEmpty()) {
        return;
    }

    for (const auto &client : m_state.clients()) {
        if (client->state != QcServerClient::State::Playing) {
            continue;
        }
        QList<QcMsg::BlockUpdate> updates;
        for (const QcMsg::BlockUpdate &update : std::as_const(m_batch)) {
            if (client->view.sentChunks.contains(update.pos.chunk())) {
                updates.append(update);
            }
        }
        // A client refuses a batch above maxUpdates (the limits allow more blocks per tick).
        const QList<QcMsg::BlockBatch> batches = pieces(updates, qsizetype(QcMsg::BlockBatch::maxUpdates));
        for (const QcMsg::BlockBatch &batch : batches) {
            client->connection->send(batch);
        }
    }
    m_batch.clear();
}

QList<QcMsg::BlockBatch> QcBlastBlocks::pieces(const QList<QcMsg::BlockUpdate> &updates, qsizetype most)
{
    QList<QcMsg::BlockBatch> batches;
    for (qsizetype start = 0; start < updates.size(); start += most) {
        QcMsg::BlockBatch batch;
        batch.updates = updates.mid(start, most);
        batches.append(batch);
    }
    return batches;
}
