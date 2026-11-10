// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcBeacon.h"
#include "common/world/QcChunk.h"

#include <QHash>
#include <QList>
#include <QString>

#include <utility>
#include <vector>

class QcContentRegistry;

/*! \brief The beacons of the loaded chunks (decision E-1142): one entry per beacon block with the
    effects chosen on its screen and, as the last look found them (QcServerBeacons), its level and
    beam.

    Kept in step with the blocks by QcServerWorld: a chunk that arrives gets an entry for each of
    its beacon blocks, a beacon set gets one and a beacon gone loses it. The chosen effects are
    saved with the chunk (QcBlockEntityRecord::Table::Beacons); level and beam are not, the next
    look finds them again. */
class QcBeacons
{
public:
    /*! \brief A beacon block and what it knows. */
    struct Entry
    {
        /*! The position of the beacon block. */
        QcBlockPos pos;
        /*! The effects chosen on its screen (effect kinds, empty for none). \qcshared */
        QString primary;
        QString secondary;
        /*! The level the last look found (0: none). */
        int level = 0;
        /*! The beam the last look found, as the clients were told (empty: none). */
        QList<QcBeamSection> beam;

        /*! Returns whether it shines: a level and a beam. */
        bool isActive() const { return level > 0 && !beam.isEmpty(); }

        /*! \brief What one look() changed. \qcfields */
        struct Change
        {
            bool wasActive = false;
            int levelBefore = 0;
            bool beamChanged = false;
        };
        /*! Looks at the beam above the beacon and, with one, at the pyramid below it, in the world
            \a blockAt tells, by the blocks of \a content (like the reference, a beacon without a
            beam keeps its level). Returns what changed. */
        Change look(const QcContentRegistry &content, const QcBeacon::BlockAt &blockAt);
        bool operator==(const Entry &) const = default;
    };

    /*! \brief What is saved of a beacon: its position and chosen effects. \qcfields */
    struct Saved
    {
        QcBlockPos pos;
        QString primary;
        QString secondary;

        bool operator==(const Saved &) const = default;
    };

    explicit QcBeacons(const QcContentRegistry &content);

    /*! Returns whether the state \a block is a beacon. */
    bool isBeacon(QcBlockId block) const { return static_cast<std::size_t>(block) < m_beacon.size() && m_beacon[static_cast<std::size_t>(block)]; }

    /*! The \a chunk at \a pos entered the world: an entry for each of its beacon blocks (sections
        whose palette has none are not looked at). */
    void noteChunk(QcChunkPos pos, const QcChunk &chunk);
    /*! The block at \a pos became \a block: a beacon gets an entry (one that is there stays), any
        other block ends the entry at \a pos. */
    void noteBlock(QcBlockPos pos, QcBlockId block);
    /*! Forgets the entries of the chunk at \a pos (it left the world or was generated again). */
    void forgetChunk(QcChunkPos pos);
    /*! Returns the beacons of the chunk at \a pos that have chosen effects, in position order, to
        be saved. */
    QList<Saved> savedOf(QcChunkPos pos) const;
    /*! Puts the chosen effects of the saved \a beacons back where a beacon still stands (their
        chunk arrived with noteChunk() before). */
    void restore(const QList<Saved> &beacons);

    /*! Returns the entry at \a pos, or null when no beacon stands there. */
    Entry *at(QcBlockPos pos);
    /*! Returns the entry at \a pos, or null when no beacon stands there. */
    const Entry *at(QcBlockPos pos) const;
    /*! Returns the positions of every beacon in position order (the looks go in this order). */
    QList<QcBlockPos> positions() const;
    /*! Returns the positions of the beacons in the chunk at \a pos. */
    QList<QcBlockPos> positionsIn(QcChunkPos pos) const { return m_byChunk.value(pos); }
    /*! Returns the number of beacons. */
    qsizetype size() const { return m_entries.size(); }
    /*! Returns the positions of the beacons that stopped shining because they are gone (broken or
        replaced) since the last call, and forgets them: the clients still draw their beams. */
    QList<QcBlockPos> takeDarkened() { return std::exchange(m_darkened, {}); }

private:
    void add(QcBlockPos pos);
    void remove(QcBlockPos pos);

    // isBeacon() per block state id.
    std::vector<bool> m_beacon;
    QHash<QcBlockPos, Entry> m_entries; // NOLINT(qc-member-access): changed in place by add(), remove() and forgetChunk() only
    QHash<QcChunkPos, QList<QcBlockPos>> m_byChunk; // NOLINT(qc-member-access): changed in place by add(), remove() and forgetChunk() only
    QList<QcBlockPos> m_darkened;
};
