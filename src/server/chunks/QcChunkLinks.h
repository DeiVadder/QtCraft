// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcBlockPos.h"
#include "common/world/QcChunk.h"

#include <QHash>

#include <array>
#include <utility>
#include <vector>

/*! \brief Which loaded chunks a circuit or machine connects across their border (decision E-375),
    kept up to date as chunks arrive and blocks are set, never computed per tick.

    A chunk has a mark per side (east +x, west -x, south +z, north -z):

    \list
    \li strip: a circuit part (any signal component but doors, trapdoors and gates) lies within
        \c {stripWidth} cells of that side;
    \li reach: a piston faces that side and could push blocks across it (within \c {pistonReach}).
    \endlist
    Two neighbouring chunks are linked when both have a strip towards each other or either
    reaches into the other. The marks of a chunk are saved with it (QcChunkStore::Stored::marks),
    so the links of chunks that are not loaded are known without reading them; linked chunks form
    a group that loads and unloads as a whole (QcChunkGroups). */
class QcChunkLinks
{
public:
    /*! The sides of a chunk; the value is the side's bit in the marks (strip; reach four bits
        higher), so it is part of the saved marks (QcChunkStore::Stored::marks). */
    enum class Side : quint8
    {
        /*! Towards +x. */
        East = 0,
        /*! Towards -x. */
        West = 1,
        /*! Towards +z. */
        South = 2,
        /*! Towards -z. */
        North = 3,
    };
    /*! The number of sides of a chunk. */
    static constexpr int sideCount = 4;
    /*! All sides of a chunk, in the order of their bits. */
    static constexpr std::array<Side, sideCount> sides{Side::East, Side::West, Side::South, Side::North};
    /*! A circuit part this many cells or fewer from a side marks its strip. */
    static constexpr int stripWidth = 2;
    /*! A piston pushes up to 12 blocks one cell on: blocks up to 13 cells ahead move. */
    static constexpr int pistonReach = 13;

    explicit QcChunkLinks(const QcContentRegistry &content);

    /*! Returns the neighbour of \a chunk across \a side. */
    static QcChunkPos neighbour(QcChunkPos chunk, Side side);
    /*! Returns the side that faces back across \a side. */
    static constexpr Side opposite(Side side) { return static_cast<Side>(std::to_underlying(side) ^ 1u); }
    /*! Returns whether a chunk with \a marks and its neighbour with \a neighbourMarks are linked
        across \a side of the first one. */
    static bool linked(quint16 marks, quint16 neighbourMarks, Side side);

    /*! \a chunk entered the world: counts its circuit parts near its sides and its pistons. */
    void noteChunk(const QcChunk &chunk);
    /*! The block at \a pos changed from \a before to \a after in a loaded chunk. */
    void noteBlock(QcBlockPos pos, QcBlockId before, QcBlockId after);
    /*! \a chunk left the world: its marks are forgotten. */
    void forgetChunk(QcChunkPos chunk);
    /*! Returns the marks of the loaded \a chunk (0 for one without). */
    quint16 marksOf(QcChunkPos chunk) const;
    /*! Returns the marks of any \a chunk, counted from its blocks. */
    quint16 marksOf(const QcChunk &chunk) const;
    /*! Returns the count of changes of the marks of loaded chunks (QcChunkLoading looks at
        groups again on a change). */
    quint64 changes() const { return m_changes; }

private:
    struct Counts
    {
        std::array<int, sideCount> strip{};
        std::array<int, sideCount> reach{};

        bool isEmpty() const;
        // Adds `delta` times what the block at local `x`, `z` adds to its chunk (by the parts and
        // pistons `links` knows).
        void add(const QcChunkLinks &links, int x, int z, QcBlockId block, int delta);
        // Adds the blocks of one section.
        void addSection(const QcChunkLinks &links, const QcChunkSection &section);
    };

    Counts countChunk(const QcChunk &chunk) const;
    static quint16 marksOf(const Counts &counts);
    bool matters(QcBlockId block) const
    {
        return static_cast<std::size_t>(block) < m_part.size()
            && (m_part[static_cast<std::size_t>(block)] || m_piston[static_cast<std::size_t>(block)] != 0);
    }

    // Per block state: a circuit part, and the side a piston faces plus one (0: none or up/down).
    std::vector<bool> m_part;
    std::vector<quint8> m_piston;
    QHash<QcChunkPos, Counts> m_counts;
    quint64 m_changes = 0;
};
