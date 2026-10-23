#include "server/chunks/QcChunkSaveState.h"

#include "server/core/QcServerLogging.h"

void QcChunkSaveState::setStorage(QcChunkIo *io, QHash<QcChunkPos, Stored> catalog)
{
    m_io = io;
    m_catalog = std::move(catalog);
}

std::optional<QcChunkSaveState::Stored> QcChunkSaveState::stored(QcChunkPos pos) const
{
    const auto it = m_catalog.constFind(pos);
    return it == m_catalog.cend() ? std::nullopt : std::optional<Stored>(*it);
}

void QcChunkSaveState::noteKept(const QcChunkIo::Departure &departure)
{
    using Kind = QcChunkStore::Kind;
    Stored stored = m_catalog.value(departure.pos);
    const auto set = [&stored](Kind kind, bool present) {
        const auto bit = static_cast<quint8>(1u << static_cast<quint8>(kind));
        stored.kinds = present ? static_cast<quint8>(stored.kinds | bit) : static_cast<quint8>(stored.kinds & ~bit);
    };
    if (departure.blocks) {
        set(Kind::Blocks, true);
    }
    set(Kind::BlockEntities, !departure.blockEntities.isEmpty() || departure.marks != 0);
    set(Kind::Entities, !departure.entities.isEmpty() || departure.entities.populated);
    stored.marks = departure.marks;
    if (stored.kinds == 0) {
        m_catalog.remove(departure.pos);
    } else {
        m_catalog.insert(departure.pos, stored);
    }
}

void QcChunkSaveState::quarantine(QcChunkPos pos, const QString &error)
{
    // Damaged data stays on disk untouched; the chunk plays as generated and is never kept.
    qCCritical(lcServer) << "Chunk" << pos.x << pos.z << "cannot be read, it plays as generated and is not saved:" << error;
    m_quarantined.insert(pos);
    m_newlyQuarantined.append(pos);
}

void QcChunkSaveState::noteArrived(QcChunkPos pos)
{
    if (m_catalog.value(pos).has(QcChunkStore::Kind::Blocks)) {
        m_differs.insert(pos);
    }
}

void QcChunkSaveState::noteRegenerated(QcChunkPos pos)
{
    m_quarantined.remove(pos);
    noteChanged(pos);
}

bool QcChunkSaveState::takeChanged(QcChunkPos pos)
{
    if (!m_changed.contains(pos) || m_quarantined.contains(pos)) {
        return false;
    }
    m_changed.remove(pos);
    return true;
}

bool QcChunkSaveState::noteLeft(QcChunkPos pos)
{
    m_differs.remove(pos);
    // A quarantined chunk is never kept (its stored data stays as it is).
    return m_changed.remove(pos) && !m_quarantined.contains(pos);
}
