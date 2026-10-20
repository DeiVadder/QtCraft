#pragma once

#include "common/core/QcChunkPos.h"
#include "server/storage/QcChunkIo.h"
#include "server/storage/QcChunkStore.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <optional>
#include <utility>

// What the storage holds of the chunks and how the loaded chunks stand towards it (part of
// QcServerWorld, decision E-716): the catalog of stored chunks, the loaded chunks whose blocks changed
// since they were last kept or differ from what the generator makes, and the chunks whose stored data
// is damaged (decision E-373). Quarantined chunks play as generated and are never kept, until
// /regenerate replaces their stored data.
class QcChunkSaveState
{
public:
    using Stored = QcChunkStore::Stored;

    // Reads and writes stored chunks through `io` (none: chunks are only generated); `catalog` is
    // what the storage holds. Does not own `io`.
    void setStorage(QcChunkIo *io, QHash<QcChunkPos, Stored> catalog);
    QcChunkIo *storage() const { return m_io; }
    // What the storage holds of a chunk, as far as the server knows (kept up to date by noteKept()).
    [[nodiscard]] std::optional<Stored> stored(QcChunkPos pos) const;
    qsizetype storedChunkCount() const { return m_catalog.size(); }
    QList<QcChunkPos> storedChunks() const { return m_catalog.keys(); }
    // Whether the storage holds data of the chunk (none without a storage).
    bool hasStored(QcChunkPos pos) const { return m_io && m_catalog.contains(pos); }
    // The circuit marks of a stored chunk (zero for one never stored).
    quint16 storedMarks(QcChunkPos pos) const { return m_catalog.value(pos).marks; }
    // What was handed to the storage for a chunk (keeps stored() up to date).
    void noteKept(const QcChunkIo::Departure &departure);

    bool isQuarantined(QcChunkPos pos) const { return m_quarantined.contains(pos); }
    // The stored data of the chunk cannot be read (`error`): logged, and the chunk plays as generated.
    void quarantine(QcChunkPos pos, const QString &error);
    // Chunks found damaged since the last call (the server tells the operators).
    QList<QcChunkPos> takeNewlyQuarantined() { return std::exchange(m_newlyQuarantined, {}); }

    // Whether the blocks of a loaded chunk differ from what the generator makes (changed while
    // loaded or read from storage).
    bool differsFromGenerated(QcChunkPos pos) const { return m_differs.contains(pos); }
    // The blocks of a loaded chunk changed (a block was set, the chunk was put in as changed).
    void noteChanged(QcChunkPos pos)
    {
        m_changed.insert(pos);
        m_differs.insert(pos);
    }
    // A chunk entered the world: it differs from the generated one when its stored blocks were read.
    void noteArrived(QcChunkPos pos);
    // A loaded chunk was generated again: its damaged data is given up, and what plays now replaces
    // the stored copy in the next save.
    void noteRegenerated(QcChunkPos pos);
    // Takes the changed mark of a chunk that is kept now (a save); false when there is nothing to
    // keep (unchanged or quarantined).
    [[nodiscard]] bool takeChanged(QcChunkPos pos);
    // The chunk left the world; true when its blocks changed since they were last kept and may be
    // kept (not quarantined).
    bool noteLeft(QcChunkPos pos);

private:
    QcChunkIo *m_io = nullptr;
    QHash<QcChunkPos, Stored> m_catalog;
    QSet<QcChunkPos> m_changed;
    QSet<QcChunkPos> m_differs;
    QSet<QcChunkPos> m_quarantined;
    QList<QcChunkPos> m_newlyQuarantined;
};
