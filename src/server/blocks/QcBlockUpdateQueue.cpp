#include "QcBlockUpdateQueue.h"

#include "QcBlockRules.h"
#include "QcServerState.h"

#include <QLoggingCategory>

#include <algorithm>
#include <array>
#include <utility>

namespace {

Q_LOGGING_CATEGORY(lcBlockUpdates, "qtcraft.server")

// Only blocks whose state depends on the horizontal neighbour: connections, wall mounts, flowing
// liquids and blocks that harden next to another one.
bool reactsToNeighbour(const QcContentRegistry &content, QcBlockId id)
{
    if (!content.isValidBlock(id)) {
        return false;
    }

    const QcBlockDefinition &definition = content.block(id);
    return !definition.connectsTo.isEmpty() || definition.support == QcBlockSupport::Attached
           || (definition.liquid && definition.flow.flows()) || !definition.hardensWith.isEmpty();
}

// Borders of oceans and lakes repeat the same pair of states over and over; the check whether
// a still source can flow across is made once per pair.
struct SourceCheck
{
    QcBlockId block = qcAirBlockId;
    std::optional<QcBlockId> neighbour;
    bool stays = false;
};

} // namespace

struct QcBlockUpdateQueue::BorderScan
{
    const QcChunk &chunk;
    QcChunkPos chunkPos;
    QcBlockFace towards = QcBlockFace::North;
    SourceCheck lastCheck;
    bool checked = false;
};

QcBlockUpdateQueue::QcBlockUpdateQueue(QcServerState &state)
    : m_state(state)
{
}

void QcBlockUpdateQueue::queue(QcBlockPos pos)
{
    if (!pos.isInsideWorldHeight()) {
        return;
    }

    if (!m_commandMode) {
        // Also when the command queue holds it: the player's change must not wait behind a backlog.
        if (!m_queuedPlayerUpdates.contains(pos)) {
            m_queuedPlayerUpdates.insert(pos);
            m_playerUpdates.push_back(pos);
        }
        return;
    }

    if (m_queuedPlayerUpdates.contains(pos) || m_queuedCommandUpdates.contains(pos)) {
        return;
    }

    if (std::cmp_greater_equal(m_commandUpdates.size(), m_state.config.maxCommandBlockUpdates)) {
        // Bounded memory and save size; a block whose update was dropped is checked again
        // by the next change next to it.
        if (m_dropped++ == 0) {
            qCWarning(lcBlockUpdates) << "Too many pending command updates; dropping further ones";
        }
        return;
    }

    m_queuedCommandUpdates.insert(pos);
    m_commandUpdates.push_back(pos);
}

void QcBlockUpdateQueue::queueNeighbours(QcBlockPos pos, bool self)
{
    if (self) {
        queue(pos);
    }

    for (const QcBlockFace face : qcAllBlockFaces) {
        queue(qcNeighbour(pos, face));
    }
}

std::optional<QcBlockUpdateQueue::Update> QcBlockUpdateQueue::takeNext()
{
    const bool fromCommand = m_playerUpdates.empty();
    std::deque<QcBlockPos> &pending = fromCommand ? m_commandUpdates : m_playerUpdates;
    if (pending.empty()) {
        return std::nullopt;
    }

    const QcBlockPos pos = pending.front();
    pending.pop_front();
    (fromCommand ? m_queuedCommandUpdates : m_queuedPlayerUpdates).remove(pos);
    ++m_processed;
    return Update{pos, fromCommand};
}

void QcBlockUpdateQueue::forgetChunk(QcChunkPos chunk)
{
    const auto inChunk = [chunk](QcBlockPos pos) { return pos.chunk() == chunk; };
    std::erase_if(m_playerUpdates, inChunk);
    std::erase_if(m_commandUpdates, inChunk);
    m_queuedPlayerUpdates.removeIf(inChunk);
    m_queuedCommandUpdates.removeIf(inChunk);
    m_waitingCount -= m_waitingUpdates.take(chunk).size();
}

void QcBlockUpdateQueue::waitForChunk(QcChunkPos chunk, QcBlockPos pos)
{
    if (m_waitingCount < maxWaitingBlockUpdates) {
        QSet<QcBlockPos> &waiting = m_waitingUpdates[chunk];
        const qsizetype before = waiting.size();
        waiting.insert(pos);
        m_waitingCount += waiting.size() - before;
    }
}

QList<QcBlockPos> QcBlockUpdateQueue::waitingUpdates() const
{
    QList<QcBlockPos> positions;
    for (const QSet<QcBlockPos> &waiting : std::as_const(m_waitingUpdates)) {
        positions.append(QList<QcBlockPos>(waiting.begin(), waiting.end()));
    }
    return positions;
}

void QcBlockUpdateQueue::handleLoadedChunks()
{
    const QList<QcChunkPos> loaded = m_state.world.takeNewlyLoaded();
    for (const QcChunkPos &chunk : loaded) {
        if (!m_state.world.chunk(chunk)) {
            continue;
        }

        // A chunk seen for the first time gets its animals (decision E-086); circuits waiting for it
        // look at their blocks (decision E-340).
        m_state.mobs.populateChunk(m_state.world, chunk, [this] { return m_state.newEntityId(); });
        m_state.circuits.chunkLoaded(m_state.world, chunk);

        m_commandMode = true;
        if (const auto waiting = m_waitingUpdates.constFind(chunk); waiting != m_waitingUpdates.constEnd()) {
            for (const QcBlockPos &pos : *waiting) {
                queue(pos);
            }
            m_waitingCount -= waiting->size();
            m_waitingUpdates.erase(waiting);
        }

        constexpr std::array<QcBlockFace, 4> sides = {QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West};
        for (const QcBlockFace side : sides) {
            const QcFaceOffset offset = qcFaceOffset(side);
            const QcChunkPos neighbour{chunk.x + offset.dx, chunk.z + offset.dz};
            if (m_state.world.chunk(neighbour)) {
                queueBorderUpdates(chunk, side);
                queueBorderUpdates(neighbour, QcBlockRules::opposite(side));
            }
        }

        m_commandMode = false;
    }
}

void QcBlockUpdateQueue::queueBorderUpdates(QcChunkPos chunkPos, QcBlockFace towards)
{
    const std::shared_ptr<const QcChunk> chunk = m_state.world.chunk(chunkPos);
    if (!chunk) {
        return;
    }

    const QcContentRegistry &content = *m_state.content;
    BorderScan scan{*chunk, chunkPos, towards, {}, false};

    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        if (std::ranges::none_of(chunk->section(index).palette(),
                                 [&content](QcBlockId id) { return reactsToNeighbour(content, id); })) {
            continue;
        }
        queueSectionBorder(scan, index);
    }
}

void QcBlockUpdateQueue::queueSectionBorder(BorderScan &scan, int sectionIndex)
{
    const QcContentRegistry &content = *m_state.content;
    const QcChunkSection &section = scan.chunk.section(sectionIndex);
    const QcFaceOffset offset = qcFaceOffset(scan.towards);
    constexpr int last = QcWorld::chunkSize - 1;

    for (int y = 0; y < QcWorld::sectionHeight; ++y) {
        for (int along = 0; along < QcWorld::chunkSize; ++along) {
            const int x = offset.dx != 0 ? (offset.dx > 0 ? last : 0) : along;
            const int z = offset.dz != 0 ? (offset.dz > 0 ? last : 0) : along;
            const QcBlockId block = section.block(x, y, z);
            if (!reactsToNeighbour(content, block)) {
                continue;
            }

            const QcBlockPos pos{scan.chunkPos.x * QcWorld::chunkSize + x, sectionIndex * QcWorld::sectionHeight + y,
                                 scan.chunkPos.z * QcWorld::chunkSize + z};
            if (stillSourceStays(scan, block, pos)) {
                continue;
            }

            queue(pos);
        }
    }
}

bool QcBlockUpdateQueue::stillSourceStays(BorderScan &scan, QcBlockId block, QcBlockPos pos) const
{
    const QcContentRegistry &content = *m_state.content;
    // A still source only matters where it can flow across the border or reacts with a
    // liquid there (oceans would otherwise queue every border cell on each load). Only the
    // neighbour across the border is looked at: the other sides lie inside a chunk that
    // was already loaded, where the source settled when it was generated or last updated.
    if (!QcLiquidFlow::flows(content, block)) {
        return false;
    }

    const std::optional<QcBlockId> neighbour = m_state.world.blockAt(qcNeighbour(pos, scan.towards));
    if (scan.checked && scan.lastCheck.block == block && scan.lastCheck.neighbour == neighbour) {
        return scan.lastCheck.stays;
    }

    bool reacts = false;
    if (neighbour && content.isValidBlock(*neighbour)) {
        const QcBlockDefinition &across = content.block(*neighbour);
        reacts = across.liquid && !content.isSameBlock(*neighbour, block)
                 && std::ranges::contains(content.block(block).flow.reactions, across.name, &QcLiquidReaction::with);
    }

    const bool stays = QcLiquidFlow::isSource(content, block) && !reacts
                       && !QcLiquidFlow::canReplace(content, neighbour, QcLiquidFlow::stateWith(content, block, 1, false));
    scan.lastCheck = SourceCheck{block, neighbour, stays};
    scan.checked = true;
    return stays;
}
