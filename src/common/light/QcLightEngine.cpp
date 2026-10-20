#include "common/light/QcLightEngine.h"

#include "common/core/QcBlockFace.h"
#include "common/core/QcSectionKey.h"
#include "common/light/QcLightCells.h"
#include "common/light/QcSkyLight.h"
#include "common/world/QcLightPassage.h"

#include <algorithm>
#include <utility>

namespace {

using QcLightCells::columnIndex;
using QcLightCells::floorChunk;
using QcLightCells::horizontalFaces;
using QcLightCells::localOf;

constexpr int last = QcWorld::chunkSize - 1;

int cellIndex(int x, int y, int z)
{
    return QcChunkSection::index(localOf(x), y % QcWorld::sectionHeight, localOf(z));
}

constexpr std::size_t channelIndex(QcLightEngine::Channel channel)
{
    return std::size_t(channel);
}

} // namespace

QcLightEngine::QcLightEngine(std::shared_ptr<const QcLightTable> table, Options options)
    : m_table(std::move(table))
    , m_options(options)
{
}

QcLightEngine::Column *QcLightEngine::column(int x, int z)
{
    const QcChunkPos pos{floorChunk(x), floorChunk(z)};
    if (m_cached && pos == m_cachedPos) {
        return m_cached;
    }
    m_cachedPos = pos;
    m_cached = find(pos);
    return m_cached;
}

const QcLightEngine::Column *QcLightEngine::column(int x, int z) const
{
    const auto it = m_columns.find({floorChunk(x), floorChunk(z)});
    return it == m_columns.end() ? nullptr : &it->second;
}

QcLightEngine::Column *QcLightEngine::find(QcChunkPos pos)
{
    const auto it = m_columns.find(pos);
    return it == m_columns.end() ? nullptr : &it->second;
}

const QcLightTable::Entry &QcLightEngine::entryAt(const Column &column, int x, int y, int z) const
{
    return m_table->entry(column.blocks->block(localOf(x), y, localOf(z)));
}

template<QcLightEngine::Channel C>
int QcLightEngine::levelIn(const Column &column, int x, int y, int z)
{
    const Section &section = column.light[channelIndex(C)][std::size_t(y / QcWorld::sectionHeight)];
    return section.cells ? section.cells->level(cellIndex(x, y, z)) : section.uniform;
}

bool QcLightEngine::Column::setLevel(std::size_t channel, int x, int y, int z, int level)
{
    Section &section = light[channel][std::size_t(y / QcWorld::sectionHeight)];
    if (!section.cells) {
        if (std::cmp_equal(level, section.uniform)) {
            return false;
        }
        section.cells = std::make_unique<QcLightNibbles>(section.uniform);
    }

    section.cells->setLevel(cellIndex(x, y, z), level);
    return true;
}

template<QcLightEngine::Channel C>
void QcLightEngine::setLevel(Column &column, int x, int y, int z, int level)
{
    if (!column.setLevel(channelIndex(C), x, y, z, level)) {
        return;
    }
    if (m_options.trackChanges) {
        touch(C, x, y, z);
    } else if (level == 0 || level == maxLevel) {
        noteCompactable(C, qcSectionKey({floorChunk(x), floorChunk(z)}, y / QcWorld::sectionHeight));
    }
}

void QcLightEngine::noteCompactable(Channel channel, quint64 key)
{
    // Propagation writes many cells of one section in a row: the set sees each run once.
    const std::pair<quint64, int> entry{key, int(channel)};
    if (m_lastCompactable == entry) {
        return;
    }
    m_compactable.insert(entry);
    m_lastCompactable = entry;
}

template<QcLightEngine::Channel C>
int QcLightEngine::emission(const Column &column, int x, int y, int z) const
{
    if constexpr (C == Channel::Block) {
        return entryAt(column, x, y, z).emission;
    } else {
        return y > column.skyTop[std::size_t(columnIndex(localOf(x), localOf(z)))] ? maxLevel : 0;
    }
}

void QcLightEngine::touch(Channel channel, int x, int y, int z)
{
    const QcChunkPos chunk{floorChunk(x), floorChunk(z)};
    const int sectionIndex = y / QcWorld::sectionHeight;
    Touched &touched = m_touched[channelIndex(channel)];
    const quint64 key = qcSectionKey(chunk, sectionIndex);
    if (!touched.keys.contains(key)) {
        touched.keys.insert(key);
        touched.refs.append({chunk, sectionIndex});
    }
}

template<QcLightEngine::Channel C>
void QcLightEngine::propagateAdd()
{
    for (std::size_t head = 0; head < m_add.size(); ++head) {
        const Cell cell = m_add[head];
        const Column *own = column(cell.x, cell.z);
        if (!own) {
            continue;
        }

        const int light = levelIn<C>(*own, cell.x, cell.y, cell.z);
        ++m_visited;
        if (light <= 1) {
            continue;
        }

        const quint8 blocked = entryAt(*own, cell.x, cell.y, cell.z).blockedFaces;
        for (const QcBlockFace face : qcAllBlockFaces) {
            const QcFaceOffset offset = qcFaceOffset(face);
            const Cell next{cell.x + offset.dx, cell.y + offset.dy, cell.z + offset.dz};
            if (next.y < 0 || next.y >= QcWorld::worldHeight) {
                continue;
            }
            Column *target = column(next.x, next.z);
            if (!target) {
                continue;
            }

            const QcLightTable::Entry &entry = entryAt(*target, next.x, next.y, next.z);
            const int candidate = light - std::max(1, int(entry.opacity));
            const bool passes = entry.opacity < maxLevel && QcLightPassage::crosses(blocked, entry.blockedFaces, face);
            if (passes && candidate > levelIn<C>(*target, next.x, next.y, next.z)) {
                setLevel<C>(*target, next.x, next.y, next.z, candidate);
                m_add.push_back(next);
            }
        }
    }

    m_add.clear();
}

template<QcLightEngine::Channel C>
void QcLightEngine::propagateRemove()
{
    // NOLINTNEXTLINE(modernize-loop-convert): removeFrom() appends to m_remove while it is walked.
    for (std::size_t head = 0; head < m_remove.size(); ++head) {
        const auto [cell, old] = m_remove[head];
        ++m_visited;
        for (const QcBlockFace face : qcAllBlockFaces) {
            const QcFaceOffset offset = qcFaceOffset(face);
            const Cell next{cell.x + offset.dx, cell.y + offset.dy, cell.z + offset.dz};
            if (next.y < 0 || next.y >= QcWorld::worldHeight) {
                continue;
            }
            if (Column *target = column(next.x, next.z)) {
                removeFrom<C>(*target, next, old);
            }
        }
    }
    m_remove.clear();

    for (const Cell &cell : m_sources) {
        Column *own = column(cell.x, cell.z);
        const int emitted = emission<C>(*own, cell.x, cell.y, cell.z);
        if (emitted > levelIn<C>(*own, cell.x, cell.y, cell.z)) {
            setLevel<C>(*own, cell.x, cell.y, cell.z, emitted);
        }
        m_add.push_back(cell);
    }
    m_sources.clear();
}

// Forced inline: runs for every neighbour of every cleared cell.
template<QcLightEngine::Channel C>
Q_ALWAYS_INLINE void QcLightEngine::removeFrom(Column &target, Cell next, int old)
{
    const int light = levelIn<C>(target, next.x, next.y, next.z);
    if (light == 0) {
        return;
    }
    if (light >= old) {
        // Lit independently: it refills the cleared cells.
        m_add.push_back(next);
        return;
    }

    // Possibly lit through the cleared cell: clear it too; what else reaches it comes back with
    // the refill.
    setLevel<C>(target, next.x, next.y, next.z, 0);
    m_remove.emplace_back(next, light);
    if (emission<C>(target, next.x, next.y, next.z) > 0) {
        m_sources.push_back(next);
    }
}

template<QcLightEngine::Channel C>
void QcLightEngine::relight(Cell cell, int lostFrom, int lostTo, int gainFrom, int gainTo)
{
    Column *own = column(cell.x, cell.z);
    const auto clear = [this, own](Cell cleared) {
        if (const int old = levelIn<C>(*own, cleared.x, cleared.y, cleared.z); old > 0) {
            setLevel<C>(*own, cleared.x, cleared.y, cleared.z, 0);
            m_remove.emplace_back(cleared, old);
        }
    };
    for (int y = lostFrom; y <= lostTo; ++y) {
        clear({cell.x, y, cell.z});
    }
    clear(cell);
    if (!m_remove.empty()) {
        propagateRemove<C>();
    }

    for (int y = gainFrom; y <= gainTo; ++y) {
        setLevel<C>(*own, cell.x, y, cell.z, maxLevel);
        m_add.push_back({cell.x, y, cell.z});
    }
    if (const int emitted = emission<C>(*own, cell.x, cell.y, cell.z); emitted > levelIn<C>(*own, cell.x, cell.y, cell.z)) {
        setLevel<C>(*own, cell.x, cell.y, cell.z, emitted);
    }
    m_add.push_back(cell);

    // Light of the neighbours may now pass into the cell.
    for (const QcBlockFace face : qcAllBlockFaces) {
        const QcFaceOffset offset = qcFaceOffset(face);
        const Cell next{cell.x + offset.dx, cell.y + offset.dy, cell.z + offset.dz};
        if (next.y >= 0 && next.y < QcWorld::worldHeight) {
            if (const Column *target = column(next.x, next.z); target && levelIn<C>(*target, next.x, next.y, next.z) > 0) {
                m_add.push_back(next);
            }
        }
    }
    propagateAdd<C>();
}

template<QcLightEngine::Channel C, typename Visit>
void QcLightEngine::visitLitBorder(const Column &column, QcChunkPos pos, QcBlockFace towards, const Column &across, Visit &&visit)
{
    const QcFaceOffset offset = qcFaceOffset(towards);
    const int lx = offset.dx > 0 ? last : 0;
    const int lz = offset.dz > 0 ? last : 0;

    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        const Section &section = column.light[channelIndex(C)][std::size_t(index)];
        const Section &beside = across.light[channelIndex(C)][std::size_t(index)];
        if (!section.cells && (section.uniform == 0 || (!beside.cells && section.uniform <= beside.uniform))) {
            continue;
        }
        // Rows of the section bottom to top, each along the border.
        for (int row = 0; row < QcWorld::sectionHeight * QcWorld::chunkSize; ++row) {
            const int y = index * QcWorld::sectionHeight + row / QcWorld::chunkSize;
            const int along = row % QcWorld::chunkSize;
            const Cell cell{pos.x * QcWorld::chunkSize + (offset.dx != 0 ? lx : along), y,
                            pos.z * QcWorld::chunkSize + (offset.dz != 0 ? lz : along)};
            if (const int light = levelIn<C>(column, cell.x, y, cell.z); light > 0) {
                visit(cell, light);
            }
        }
    }
}

template<QcLightEngine::Channel C>
void QcLightEngine::queueBorder(QcChunkPos from, QcBlockFace towards)
{
    const Column *source = find(from);
    const QcFaceOffset offset = qcFaceOffset(towards);
    const Column *across = find({from.x + offset.dx, from.z + offset.dz});
    if (!source || !across) {
        return;
    }
    // Only cells brighter than their neighbour across by more than one step can light it.
    visitLitBorder<C>(*source, from, towards, *across, [this, across, offset](Cell cell, int light) {
        if (light - 1 > levelIn<C>(*across, cell.x + offset.dx, cell.y, cell.z + offset.dz)) {
            m_add.push_back(cell);
        }
    });
}

template<QcLightEngine::Channel C>
void QcLightEngine::queueRemovalSeeds(const Column &leaving, QcChunkPos pos, QcBlockFace towards, const Column &across)
{
    // A neighbour cell at least as bright as the border cell beside it got nothing from it.
    const QcFaceOffset offset = qcFaceOffset(towards);
    std::vector<std::pair<Cell, int>> &seeds = m_seeds[channelIndex(C)];
    visitLitBorder<C>(leaving, pos, towards, across, [&seeds, &across, offset](Cell cell, int light) {
        if (levelIn<C>(across, cell.x + offset.dx, cell.y, cell.z + offset.dz) < light) {
            seeds.emplace_back(cell, light);
        }
    });
}

void QcLightEngine::seedBlockSources(Column &column, QcChunkPos pos)
{
    const auto emits = [this](QcBlockId id) { return m_table->entry(id).emission > 0; };
    for (int sectionIndex = 0; sectionIndex < QcWorld::sectionCount; ++sectionIndex) {
        const QcChunkSection &section = column.blocks->section(sectionIndex);
        if (!std::ranges::any_of(section.palette(), emits)) {
            continue;
        }
        // Cells in the order y, z, x (x fastest).
        for (int index = 0; index < QcWorld::blocksPerSection; ++index) {
            const int lx = index % QcWorld::chunkSize;
            const int lz = (index / QcWorld::chunkSize) % QcWorld::chunkSize;
            const int ly = index / (QcWorld::chunkSize * QcWorld::chunkSize);
            const int emitted = m_table->entry(section.block(lx, ly, lz)).emission;
            if (emitted > 0) {
                const Cell cell{pos.x * QcWorld::chunkSize + lx, sectionIndex * QcWorld::sectionHeight + ly,
                                pos.z * QcWorld::chunkSize + lz};
                setLevel<Channel::Block>(column, cell.x, cell.y, cell.z, emitted);
                m_add.push_back(cell);
            }
        }
    }
}

quint32 QcLightEngine::Column::seedSky(const SkyTops &tops)
{
    static_assert(QcWorld::sectionCount <= 32, "one bit per section");
    skyTop = tops;
    const auto [lowestTop, highestTop] = std::ranges::minmax(tops);
    const int lowest = lowestTop;
    const int highest = highestTop;
    quint32 lit = 0;
    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        Section &section = light[std::size_t(Channel::Sky)][std::size_t(index)];
        const int low = index * QcWorld::sectionHeight;
        const int high = low + QcWorld::sectionHeight - 1;

        // Sections above every top are lit by the open sky, sections below every top start dark.
        section.cells.reset();
        section.uniform = low > highest ? quint8(maxLevel) : 0;
        if (low > highest || high <= lowest) {
            continue;
        }

        section.cells = QcSkyLight::sectionCells(skyTop, low, high);
        lit |= quint32(1) << index;
    }
    return lit;
}

void QcLightEngine::seedSky(Column &column, QcChunkPos pos)
{
    const quint32 lit = column.seedSky(QcSkyLight::tops(*column.blocks, *m_table));
    if (!m_options.trackChanges) {
        return;
    }
    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        if (lit & (quint32(1) << index)) {
            touch(Channel::Sky, pos.x * QcWorld::chunkSize, index * QcWorld::sectionHeight, pos.z * QcWorld::chunkSize);
        }
    }
}

void QcLightEngine::queueSkyEdges(const Column &column, QcChunkPos pos)
{
    // Top of the column (lx, lz) of this chunk or of a loaded neighbour; -1 when not loaded.
    const auto topAt = [this, &column, pos](int lx, int lz) {
        if (lx >= 0 && lx < QcWorld::chunkSize && lz >= 0 && lz < QcWorld::chunkSize) {
            return int(column.skyTop[std::size_t(columnIndex(lx, lz))]);
        }
        const Column *other = find({pos.x + floorChunk(lx), pos.z + floorChunk(lz)});
        return other ? int(other->skyTop[std::size_t(columnIndex(localOf(lx), localOf(lz)))]) : -1;
    };
    QcSkyLight::forEachEdgeCell(column.skyTop, topAt, [this, pos](int lx, int y, int lz) {
        m_add.push_back({pos.x * QcWorld::chunkSize + lx, y, pos.z * QcWorld::chunkSize + lz});
    });
}

void QcLightEngine::compactColumn(Column &column)
{
    for (auto &channel : column.light) {
        for (Section &section : channel) {
            if (!section.cells) {
                continue;
            }
            if (const int level = section.cells->uniformLevel(); level >= 0) {
                section.cells.reset();
                section.uniform = quint8(level);
            }
        }
    }
}

void QcLightEngine::compactWritten()
{
    for (const auto &[key, channel] : std::as_const(m_compactable)) {
        const QcSectionAddress address = qcSectionFromKey(key);
        compact(Channel(channel), address.chunk, address.sectionIndex);
    }
    m_compactable.clear();
    m_lastCompactable.reset();
}

void QcLightEngine::insert(QcChunkPos pos, const QcChunk &blocks, std::shared_ptr<const QcChunk> owner)
{
    if (m_columns.contains(pos)) {
        removeChunk(pos);
    }
    Column &added = m_columns[pos];
    added.blocks = &blocks;
    added.owner = std::move(owner);
    m_cached = nullptr;

    seedBlockSources(added, pos);
    // Light of the neighbours flows in over the borders.
    for (const QcBlockFace face : horizontalFaces) {
        const QcFaceOffset offset = qcFaceOffset(face);
        queueBorder<Channel::Block>({pos.x + offset.dx, pos.z + offset.dz}, QcLightPassage::opposite(face));
    }
    propagateAdd<Channel::Block>();

    if (m_options.sky) {
        seedSky(added, pos);
        queueSkyEdges(added, pos);
        for (const QcBlockFace face : horizontalFaces) {
            const QcFaceOffset offset = qcFaceOffset(face);
            queueBorder<Channel::Sky>({pos.x + offset.dx, pos.z + offset.dz}, QcLightPassage::opposite(face));
        }
        propagateAdd<Channel::Sky>();
    }
    if (!m_options.trackChanges) {
        compactColumn(added);
        compactWritten();
    }
}

void QcLightEngine::addChunk(std::shared_ptr<const QcChunk> chunk)
{
    const QcChunk &blocks = *chunk;
    insert(blocks.pos(), blocks, std::move(chunk));
}

void QcLightEngine::addChunk(const QcChunk &chunk)
{
    insert(chunk.pos(), chunk, nullptr);
}

std::shared_ptr<QcLightEngine::Prepared> QcLightEngine::prepare(const QcChunk &chunk) const
{
    QcLightEngine alone(m_table, {.sky = m_options.sky});
    alone.addChunk(chunk);
    auto prepared = std::make_shared<Prepared>();
    prepared->m_pos = chunk.pos();
    prepared->m_column = std::move(alone.m_columns.begin()->second);
    return prepared;
}

template<QcLightEngine::Channel C>
void QcLightEngine::joinBorders(QcChunkPos pos)
{
    for (const QcBlockFace face : horizontalFaces) {
        const QcFaceOffset offset = qcFaceOffset(face);
        queueBorder<C>({pos.x + offset.dx, pos.z + offset.dz}, QcLightPassage::opposite(face));
        queueBorder<C>(pos, face);
    }
    propagateAdd<C>();
}

void QcLightEngine::addChunk(const QcChunk &chunk, const std::shared_ptr<Prepared> &prepared)
{
    const QcChunkPos pos = chunk.pos();
    if (!prepared || prepared->m_pos != pos || m_options.trackChanges) {
        insert(pos, chunk, nullptr);
        return;
    }
    if (m_columns.contains(pos)) {
        removeChunk(pos);
    }

    Column &added = m_columns[pos];
    added = std::move(prepared->m_column);
    added.blocks = &chunk;
    added.owner.reset();
    m_cached = nullptr;
    joinBorders<Channel::Block>(pos);
    if (m_options.sky) {
        joinBorders<Channel::Sky>(pos);
    }
    compactWritten();
}

void QcLightEngine::removeChunk(QcChunkPos pos)
{
    const auto it = m_columns.find(pos);
    if (it == m_columns.end()) {
        return;
    }

    // The light of the border cells is what flowed (or could have flowed) into the neighbours.
    for (const QcBlockFace face : horizontalFaces) {
        const QcFaceOffset offset = qcFaceOffset(face);
        const Column *across = find({pos.x + offset.dx, pos.z + offset.dz});
        if (!across) {
            continue;
        }
        queueRemovalSeeds<Channel::Block>(it->second, pos, face, *across);
        if (m_options.sky) {
            queueRemovalSeeds<Channel::Sky>(it->second, pos, face, *across);
        }
    }

    m_columns.erase(it);
    m_cached = nullptr;
    // Swapped, not moved: the seed lists and the removal queue keep their capacity for the next call.
    Q_ASSERT(m_remove.empty());
    m_remove.swap(m_seeds[channelIndex(Channel::Block)]);
    propagateRemove<Channel::Block>();
    propagateAdd<Channel::Block>();
    m_remove.swap(m_seeds[channelIndex(Channel::Sky)]);
    propagateRemove<Channel::Sky>();
    propagateAdd<Channel::Sky>();
    compactWritten();
}

void QcLightEngine::change(QcBlockPos pos, QcBlockId previous, const QcChunk &blocks, std::shared_ptr<const QcChunk> owner)
{
    Column *own = find(pos.chunk());
    if (!own) {
        return;
    }
    const QcLightTable::Entry before = m_table->entry(previous);
    own->blocks = &blocks;
    own->owner = std::move(owner);
    if (!pos.isInsideWorldHeight()) {
        return;
    }

    const QcLightTable::Entry &after = entryAt(*own, pos.x, pos.y, pos.z);
    const bool passage = before.passageDiffers(after);
    const Cell cell{pos.x, pos.y, pos.z};
    if (passage || before.emission != after.emission) {
        relight<Channel::Block>(cell, 0, -1, 0, -1);
    }
    if (!m_options.sky || !passage) {
        compactWritten();
        return;
    }

    // The sources of the column move when the change stops sky light differently.
    const std::size_t index = std::size_t(columnIndex(pos.localX(), pos.localZ()));
    const int oldTop = own->skyTop[index];
    const int newTop = before.skyStop == after.skyStop ? oldTop : QcSkyLight::columnTop(blocks, *m_table, pos.localX(), pos.localZ());
    own->skyTop[index] = qint16(newTop);
    relight<Channel::Sky>(cell, oldTop + 1, newTop, newTop + 1, oldTop);
    compactWritten();
}

void QcLightEngine::changeBlock(QcBlockPos pos, std::shared_ptr<const QcChunk> chunk)
{
    const QcChunk &blocks = *chunk;
    const Column *own = find(pos.chunk());
    const QcBlockId before = own && pos.isInsideWorldHeight() ? own->blocks->block(pos.localX(), pos.y, pos.localZ()) : qcAirBlockId;
    change(pos, before, blocks, std::move(chunk));
}

void QcLightEngine::changeBlock(QcBlockPos pos, QcBlockId before, const QcChunk &chunk)
{
    change(pos, before, chunk, nullptr);
}

void QcLightEngine::rebind(const QcChunk &chunk)
{
    if (Column *own = find(chunk.pos())) {
        own->blocks = &chunk;
        own->owner.reset();
    }
}

int QcLightEngine::level(Channel channel, QcBlockPos pos) const
{
    if (channel == Channel::Sky && !m_options.sky) {
        return 0;
    }
    if (pos.y >= QcWorld::worldHeight) {
        return channel == Channel::Sky ? maxLevel : 0;
    }
    const Column *own = column(pos.x, pos.z);
    if (!own || pos.y < 0) {
        return 0;
    }
    return channel == Channel::Sky ? levelIn<Channel::Sky>(*own, pos.x, pos.y, pos.z) : levelIn<Channel::Block>(*own, pos.x, pos.y, pos.z);
}

const QcLightNibbles *QcLightEngine::section(Channel channel, QcChunkPos pos, int index) const
{
    const auto it = m_columns.find(pos);
    return it == m_columns.end() ? nullptr : it->second.light[channelIndex(channel)][std::size_t(index)].cells.get();
}

int QcLightEngine::uniformLevel(Channel channel, QcChunkPos pos, int index) const
{
    const auto it = m_columns.find(pos);
    return it == m_columns.end() ? 0 : it->second.light[channelIndex(channel)][std::size_t(index)].uniform;
}

void QcLightEngine::compact(Channel channel, QcChunkPos pos, int index)
{
    const auto it = m_columns.find(pos);
    if (it == m_columns.end()) {
        return;
    }
    Section &section = it->second.light[channelIndex(channel)][std::size_t(index)];
    if (section.cells) {
        if (const int level = section.cells->uniformLevel(); level >= 0) {
            section.cells.reset();
            section.uniform = quint8(level);
        }
    }
}

QList<QcLightEngine::SectionRef> QcLightEngine::takeTouched(Channel channel)
{
    Touched &touched = m_touched[channelIndex(channel)];
    touched.keys.clear();
    return std::exchange(touched.refs, {});
}

int QcLightEngine::skyTop(QcBlockPos column) const
{
    const auto it = m_columns.find(column.chunk());
    return it == m_columns.end() ? QcWorld::worldHeight : int(it->second.skyTop[std::size_t(columnIndex(column.localX(), column.localZ()))]);
}

QcLightEngine::Memory QcLightEngine::memory() const
{
    Memory memory;
    memory.columns = qsizetype(m_columns.size());
    for (const auto &[pos, column] : m_columns) {
        for (std::size_t channel = 0; channel < channelCount; ++channel) {
            memory.sections[channel] += std::ranges::count_if(column.light[channel], [](const Section &section) { return bool(section.cells); });
        }
    }
    memory.bytes = memory.columns * qsizetype(sizeof(Column)) + (memory.sections[0] + memory.sections[1]) * qsizetype(sizeof(QcLightNibbles));
    return memory;
}
