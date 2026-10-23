#pragma once

#include "common/core/QcBlockPos.h"
#include "common/light/QcLightNibbles.h"
#include "common/light/QcLightTable.h"
#include "common/light/QcSkyLight.h"
#include "common/world/QcChunk.h"

#include <QList>
#include <QSet>

#include <array>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

// Block and sky light of all loaded chunks per section, kept up to date incrementally (decisions
// E-049 and E-389); the server and the client use the same engine.
//
// Block light shines from the blocks that give off light. Sky light shines with 15 into every cell
// above the highest cell of its column that keeps sky light coming straight down out
// (QcLightPassage::skyStop) and spreads from there like block light. Every step into a cell costs
// max(1, opacity) levels, opacity 15 stops light, no step crosses a face a partial shape covers, and
// unloaded chunks are dark walls. The result of each channel is the unique fixpoint "brightest
// source minus cheapest path", so it equals one full flood fill of the loaded chunks, whatever the
// order of the changes: a new chunk floods its own sources and the light flowing in over its
// borders; a changed block clears the light that depended on it (breadth-first removal) and refills
// the cleared cells from their lit boundary and from sources; an unloaded chunk takes away the
// light that came from it.
//
// Levels are kept as nibbles per section; a section whose cells all have the same level (all dark,
// or all lit by the open sky) holds no cells. Not thread-safe: one thread uses it.
class QcLightEngine
{
public:
    enum class Channel : quint8 { Block = 0, Sky = 1 };
    static constexpr int channelCount = 2;
    static constexpr int maxLevel = QcLightTable::maxLevel;

    struct Options
    {
        // Without sky light only block light is kept (the client's mesher computes sky light itself).
        bool sky = true;
        // Remembers the sections whose levels were written, for takeTouched().
        bool trackChanges = false;
    };
    struct SectionRef
    {
        QcChunkPos chunk;
        int section = 0;
    };
    // Sections that hold cells and the bytes they take, per channel.
    struct Memory
    {
        qsizetype columns = 0;
        std::array<qsizetype, channelCount> sections{};
        qsizetype bytes = 0;
    };

    // The light of one chunk computed on its own (its neighbours dark walls), on any thread; adding
    // the chunk with it leaves only the light across its borders to the engine's thread.
    class Prepared;

    QcLightEngine(std::shared_ptr<const QcLightTable> table, Options options);

    // Thread-safe (reads only the table): the light of `chunk` alone, for addChunk().
    std::shared_ptr<Prepared> prepare(const QcChunk &chunk) const;

    // The engine keeps a snapshot of the chunk.
    void addChunk(std::shared_ptr<const QcChunk> chunk);
    // The caller keeps `chunk` alive and in place until it hands over another copy (changeBlock,
    // rebind) or removes it.
    void addChunk(const QcChunk &chunk);
    // As above with the light prepare() computed for the same blocks (null or another chunk's:
    // computed here).
    void addChunk(const QcChunk &chunk, const std::shared_ptr<Prepared> &prepared);
    void removeChunk(QcChunkPos pos);
    // `chunk` holds the blocks of the chunk at `pos` after the change (same ownership rules as
    // addChunk); the block before the change is read from the snapshot kept so far.
    void changeBlock(QcBlockPos pos, std::shared_ptr<const QcChunk> chunk);
    // The block at `pos` was `before` and `chunk` (changed in place or a new copy) holds the new one.
    void changeBlock(QcBlockPos pos, QcBlockId before, const QcChunk &chunk);
    // The chunk's blocks moved to another, equal copy (copy on write); its light stays.
    void rebind(const QcChunk &chunk);

    const QcLightTable &table() const { return *m_table; }
    const std::shared_ptr<const QcLightTable> &sharedTable() const { return m_table; }
    bool hasSky() const { return m_options.sky; }
    bool hasChunk(QcChunkPos pos) const { return m_columns.contains(pos); }
    // Current level; 0 in unloaded chunks and without the channel. Above the world the sky is 15.
    int level(Channel channel, QcBlockPos pos) const;
    int blockLight(QcBlockPos pos) const { return level(Channel::Block, pos); }
    int skyLight(QcBlockPos pos) const { return level(Channel::Sky, pos); }
    // The cells of a section; null when they all have the level uniformLevel() (or it is not loaded).
    const QcLightNibbles *section(Channel channel, QcChunkPos pos, int index) const;
    int uniformLevel(Channel channel, QcChunkPos pos, int index) const;
    // Lets a section whose cells all have the same level go without cells.
    void compact(Channel channel, QcChunkPos pos, int index);
    // Sections whose levels were written since the last call (Options::trackChanges).
    QList<SectionRef> takeTouched(Channel channel);
    // Highest cell of a column that sky light coming straight down does not reach (-1: none).
    int skyTop(QcBlockPos column) const;
    // Cells visited by the propagation steps so far (work measure for benchmarks).
    quint64 visitedCells() const { return m_visited; }
    Memory memory() const;

private:
    struct Section
    {
        std::unique_ptr<QcLightNibbles> cells;
        // Level of every cell while `cells` is null.
        quint8 uniform = 0;
    };
    using SkyTops = QcSkyLight::Tops;
    struct Column
    {
        const QcChunk *blocks = nullptr;
        std::shared_ptr<const QcChunk> owner;
        std::array<std::array<Section, QcWorld::sectionCount>, channelCount> light;
        SkyTops skyTop{};

        // Sets the level of the cell at world `x`, `y`, `z` (inside this column) in `channel`; false
        // when nothing is written because its section is uniform at that level already.
        [[nodiscard]] bool setLevel(std::size_t channel, int x, int y, int z, int level);
        // Takes `tops` as the sky tops and lights the sky channel as the open sky does: sections above
        // every top full, sections below every top dark, the others cell by cell. Returns the
        // sections lit cell by cell, one bit per section index.
        [[nodiscard]] quint32 seedSky(const SkyTops &tops);
    };
    struct Cell
    {
        int x = 0;
        int y = 0;
        int z = 0;
    };
    struct PosHash
    {
        std::size_t operator()(QcChunkPos pos) const { return static_cast<std::size_t>(qHash(pos)); }
    };
    struct Touched
    {
        QSet<quint64> keys;
        QList<SectionRef> refs;
    };

    Column *column(int x, int z);
    const Column *column(int x, int z) const;
    Column *find(QcChunkPos pos);
    const QcLightTable::Entry &entryAt(const Column &column, int x, int y, int z) const;
    template<Channel C>
    static int levelIn(const Column &column, int x, int y, int z);
    template<Channel C>
    void setLevel(Column &column, int x, int y, int z, int level);
    template<Channel C>
    int emission(const Column &column, int x, int y, int z) const;
    void touch(Channel channel, int x, int y, int z);

    // Floods the add queue, then empties it.
    template<Channel C>
    void propagateAdd();
    // Clears the light that depended on the removal queue's cells; boundary cells and cleared
    // sources go to the add queue.
    template<Channel C>
    void propagateRemove();
    // Clears `next` (in `target`) when it was lit through a cleared cell of level `old`, else
    // queues it to refill; cleared light sources go to m_sources.
    template<Channel C>
    void removeFrom(Column &target, Cell next, int old);
    // The cell `cell` changed how light passes or what it gives off: clears it and the cells of its
    // column from lostFrom to lostTo (sky sources no more), refills around them and lights the
    // cells from gainFrom to gainTo (new sky sources) fully. Empty ranges have from > to.
    template<Channel C>
    void relight(Cell cell, int lostFrom, int lostTo, int gainFrom, int gainTo);

    // Queues the lit border cells of the column at `from` on its side `towards` that may light the
    // neighbour there.
    template<Channel C>
    void queueBorder(QcChunkPos from, QcBlockFace towards);
    // Calls visit(cell, level) for every lit cell of `column` (at `pos`) on its side `towards`, except
    // in sections where both it and the neighbour `across` there have one level each and the
    // neighbour's is at least as bright (nothing flows either way).
    template<Channel C, typename Visit>
    static void visitLitBorder(const Column &column, QcChunkPos pos, QcBlockFace towards, const Column &across, Visit &&visit);
    // Queues the lit border cells of a leaving column on its side `towards` that may have lit the
    // neighbour `across` there as seeds of the removal (m_seeds of the channel).
    template<Channel C>
    void queueRemovalSeeds(const Column &leaving, QcChunkPos pos, QcBlockFace towards, const Column &across);
    void seedBlockSources(Column &column, QcChunkPos pos);
    void seedSky(Column &column, QcChunkPos pos);
    // Queues the sky-lit cells of a column that border darker cells of its neighbour columns.
    void queueSkyEdges(const Column &column, QcChunkPos pos);
    void insert(QcChunkPos pos, const QcChunk &blocks, std::shared_ptr<const QcChunk> owner);
    // Joins a column whose own light is complete with its neighbours: light flows over its borders
    // both ways.
    template<Channel C>
    void joinBorders(QcChunkPos pos);
    void change(QcBlockPos pos, QcBlockId previous, const QcChunk &blocks, std::shared_ptr<const QcChunk> owner);
    void compactColumn(Column &column);
    // Remembers that a section of `channel` had a cell set to 0 or the highest level.
    void noteCompactable(Channel channel, quint64 key);
    // Lets the sections noted since the last call go without cells where possible.
    void compactWritten();

    std::shared_ptr<const QcLightTable> m_table;
    Options m_options;
    std::unordered_map<QcChunkPos, Column, PosHash> m_columns;
    std::vector<Cell> m_add;
    std::vector<std::pair<Cell, int>> m_remove;
    // Light sources cleared by a removal, refilled after it (kept between calls for its capacity).
    std::vector<Cell> m_sources;
    // Removal seeds of a leaving column per channel (kept between calls for their capacity).
    std::array<std::vector<std::pair<Cell, int>>, channelCount> m_seeds;
    std::array<Touched, channelCount> m_touched;
    // Sections (key and channel) where cells were set to 0 or the highest level, the levels a whole
    // section takes when it goes dark or opens to the sky; without change tracking they are compacted
    // after every added, removed or changed chunk (with it, the owner of the snapshots compacts).
    // Sections that become uniform at another level keep their cells.
    QSet<std::pair<quint64, int>> m_compactable;
    // The entry inserted last, so a run of writes into one section hashes it once.
    std::optional<std::pair<quint64, int>> m_lastCompactable;
    quint64 m_visited = 0;
    // Last chunk looked up (propagation stays mostly within one chunk).
    QcChunkPos m_cachedPos;
    Column *m_cached = nullptr;
};

class QcLightEngine::Prepared
{
private:
    friend class QcLightEngine;
    QcChunkPos m_pos;
    Column m_column;
};
