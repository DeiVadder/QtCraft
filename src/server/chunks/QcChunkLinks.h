#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcBlockPos.h"
#include "common/world/QcChunk.h"

#include <QHash>

#include <array>
#include <vector>

// Which loaded chunks a circuit or machine connects across their border (decision E-375), kept up
// to date as chunks arrive and blocks are set, never computed per tick. A chunk has a mark per side
// (east +x, west -x, south +z, north -z):
//   - strip: a circuit part (any signal component but doors, trapdoors and gates) lies within
//     `stripWidth` cells of that side;
//   - reach: a piston faces that side and could push blocks across it (within `pistonReach`).
// Two neighbouring chunks are linked when both have a strip towards each other or either reaches
// into the other. The marks of a chunk are saved with it (QcChunkStore::Stored::marks), so the links
// of chunks that are not loaded are known without reading them; linked chunks form a group that
// loads and unloads as a whole (QcChunkGroups).
class QcChunkLinks
{
public:
    static constexpr int sideCount = 4;
    static constexpr int stripWidth = 2;
    // A piston pushes up to 12 blocks one cell on: blocks up to 13 cells ahead move.
    static constexpr int pistonReach = 13;

    explicit QcChunkLinks(const QcContentRegistry &content);

    // The neighbour of `chunk` across `side` and the side that faces back.
    static QcChunkPos neighbour(QcChunkPos chunk, int side);
    static int opposite(int side) { return side ^ 1; }
    // Whether chunks with these marks are linked across `side` of the first one.
    static bool linked(quint16 marks, quint16 neighbourMarks, int side);

    // A chunk entered the world: counts its circuit parts near its sides and its pistons.
    void noteChunk(const QcChunk &chunk);
    void noteBlock(QcBlockPos pos, QcBlockId before, QcBlockId after);
    void forgetChunk(QcChunkPos chunk);
    // The marks of a loaded chunk (0 for one without).
    quint16 marksOf(QcChunkPos chunk) const;
    // The marks of any chunk, counted from its blocks.
    quint16 marksOf(const QcChunk &chunk) const;
    // Counts changes of the marks of loaded chunks (QcChunkLoading looks at groups again on a change).
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
