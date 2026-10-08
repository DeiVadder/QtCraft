#pragma once

#include "QcBlockPos.h"
#include "QcBlockRenderTable.h"
#include "QcChunk.h"
#include "QcChunkLight.h"

#include <QList>
#include <QSet>

#include <memory>
#include <unordered_map>
#include <vector>

// Block light of all loaded chunks, kept up to date incrementally (decision E-049): a new chunk
// floods its own sources and the light flowing in over its borders; a changed block clears the light
// that depended on it (breadth-first removal) and refills the cleared cells from their lit
// boundary and from sources; an unloaded chunk takes away the light that came from it. Every step
// costs max(1, opacity) levels, opacity 15 blocks, unloaded chunks are dark walls. The result
// equals a full flood fill of the loaded chunks (QcLightVolume). Not thread-safe: one thread uses it.
class QcBlockLightEngine
{
public:
    struct SectionRef
    {
        QcChunkPos chunk;
        int section = 0;
    };
    struct SectionUpdate
    {
        QcChunkPos chunk;
        int section = 0;
        // Null when the section is dark.
        std::shared_ptr<const QcChunkLight::Section> light;
    };
    // What changed since the last takeChanges(): new snapshots of sections whose levels differ,
    // and the sections whose meshes read a changed cell (also neighbours of changed border cells).
    struct Changes
    {
        QList<SectionUpdate> sections;
        QList<SectionRef> remesh;
    };

    explicit QcBlockLightEngine(std::shared_ptr<const QcBlockRenderTable> table);

    void addChunk(std::shared_ptr<const QcChunk> chunk);
    void removeChunk(QcChunkPos pos);
    // `chunk` is the snapshot of the chunk at `pos` after the change.
    void changeBlock(QcBlockPos pos, std::shared_ptr<const QcChunk> chunk);
    Changes takeChanges();

    bool hasChunk(QcChunkPos pos) const { return m_columns.contains(pos); }
    // The published light of a chunk (as of the last takeChanges()).
    QcChunkLight snapshot(QcChunkPos pos) const;
    // Current level; 0 in unloaded chunks.
    int level(QcBlockPos pos) const;
    // Cells visited by the propagation steps so far (work measure for benchmarks).
    quint64 visitedCells() const { return m_visited; }

private:
    struct Column
    {
        std::shared_ptr<const QcChunk> blocks;
        std::array<std::unique_ptr<QcChunkLight::Section>, QcWorld::sectionCount> levels;
        std::array<std::shared_ptr<const QcChunkLight::Section>, QcWorld::sectionCount> published;
    };
    struct Cell
    {
        int x = 0;
        int y = 0;
        int z = 0;
    };
    struct PosHash
    {
        std::size_t operator()(QcChunkPos pos) const { return std::size_t(qHash(pos)); }
    };

    Column *column(int x, int z);
    const Column *column(int x, int z) const;
    static int levelIn(const Column &column, int x, int y, int z);
    void setLevel(Column &column, int x, int y, int z, int level);
    const QcBlockRenderTable::Entry &entryAt(const Column &column, int x, int y, int z) const;
    // Floods the add queue, then empties it.
    void propagateAdd();
    // Clears the light that depended on the removal queue's cells; boundary cells and cleared
    // sources go to the add queue.
    void propagateRemove();
    void queueBorder(QcChunkPos from, QcBlockFace towards);

    std::shared_ptr<const QcBlockRenderTable> m_table;
    std::unordered_map<QcChunkPos, Column, PosHash> m_columns;
    std::vector<Cell> m_add;
    std::vector<std::pair<Cell, int>> m_remove;
    QSet<quint64> m_touched;
    QList<SectionRef> m_touchedRefs;
    quint64 m_visited = 0;
    // Last chunk looked up (propagation stays mostly within one chunk).
    QcChunkPos m_cachedPos;
    Column *m_cached = nullptr;
};
