// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"
#include "server/storage/QcChunkIo.h"
#include "server/storage/QcChunkStore.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <optional>
#include <utility>

/*! \brief What the storage holds of the chunks and how the loaded chunks stand towards it (part of
    QcServerWorld, decision E-716): the catalog of stored chunks, the loaded chunks whose blocks
    changed since they were last kept or differ from what the generator makes, and the chunks whose
    stored data is damaged (decision E-373).

    Quarantined chunks play as generated and are never kept, until /regenerate replaces their
    stored data. */
class QcChunkSaveState
{
public:
    /*! What the storage holds of a chunk. */
    using Stored = QcChunkStore::Stored;

    /*! Reads and writes stored chunks through \a io (none: chunks are only generated);
        \a catalog is what the storage holds. Does not own \a io. */
    void setStorage(QcChunkIo *io, QHash<QcChunkPos, Stored> catalog);
    /*! Returns the storage; nullptr when chunks are only generated. */
    QcChunkIo *storage() const { return m_io; }
    /*! Returns what the storage holds of the chunk \a pos, as far as the server knows (kept up
        to date by noteKept()). */
    [[nodiscard]] std::optional<Stored> stored(QcChunkPos pos) const;
    /*! Returns the number of stored chunks. */
    qsizetype storedChunkCount() const { return m_catalog.size(); }
    /*! Returns the stored chunks. */
    QList<QcChunkPos> storedChunks() const { return m_catalog.keys(); }
    /*! Returns whether the storage holds data of the chunk \a pos (none without a storage). */
    bool hasStored(QcChunkPos pos) const { return m_io && m_catalog.contains(pos); }
    /*! Returns the circuit marks of the stored chunk \a pos (zero for one never stored). */
    quint16 storedMarks(QcChunkPos pos) const { return m_catalog.value(pos).marks; }
    /*! \a departure was handed to the storage for a chunk (keeps stored() up to date). */
    void noteKept(const QcChunkIo::Departure &departure);

    /*! Returns whether the stored data of the chunk \a pos is damaged. */
    bool isQuarantined(QcChunkPos pos) const { return m_quarantined.contains(pos); }
    /*! The stored data of the chunk \a pos cannot be read (\a error): logged, and the chunk
        plays as generated. */
    void quarantine(QcChunkPos pos, const QString &error);
    /*! Returns the chunks found damaged since the last call (the server tells the operators). */
    QList<QcChunkPos> takeNewlyQuarantined() { return std::exchange(m_newlyQuarantined, {}); }

    /*! Returns whether the blocks of the loaded chunk \a pos differ from what the generator makes
        (changed while loaded or read from storage). */
    bool differsFromGenerated(QcChunkPos pos) const { return m_differs.contains(pos); }
    /*! The blocks of the loaded chunk \a pos changed (a block was set, the chunk was put in as
        changed). */
    void noteChanged(QcChunkPos pos)
    {
        m_changed.insert(pos);
        m_differs.insert(pos);
    }
    /*! The chunk \a pos entered the world: it differs from the generated one when its stored
        blocks were read. */
    void noteArrived(QcChunkPos pos);
    /*! The loaded chunk \a pos was generated again: its damaged data is given up, and what plays
        now replaces the stored copy in the next save. */
    void noteRegenerated(QcChunkPos pos);
    /*! Takes the changed mark of the chunk \a pos that is kept now (a save). Returns \c false when
        there is nothing to keep (unchanged or quarantined). */
    [[nodiscard]] bool takeChanged(QcChunkPos pos);
    /*! The chunk \a pos left the world. Returns \c true when its blocks changed since they were
        last kept and may be kept (not quarantined). */
    bool noteLeft(QcChunkPos pos);

private:
    QcChunkIo *m_io = nullptr;
    QHash<QcChunkPos, Stored> m_catalog;
    QSet<QcChunkPos> m_changed;
    QSet<QcChunkPos> m_differs;
    QSet<QcChunkPos> m_quarantined;
    QList<QcChunkPos> m_newlyQuarantined;
};
