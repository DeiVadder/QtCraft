#include "QcBlockLightEngine.h"

#include "QcBlockFace.h"
#include "QcSectionKey.h"

#include <algorithm>

namespace {

constexpr int maxLevel = 15;
constexpr int last = QcWorld::chunkSize - 1;

int floorChunk(int block)
{
    return block >= 0 ? block / QcWorld::chunkSize : (block + 1) / QcWorld::chunkSize - 1;
}

int localOf(int block)
{
    return block - floorChunk(block) * QcWorld::chunkSize;
}

} // namespace

QcBlockLightEngine::QcBlockLightEngine(std::shared_ptr<const QcBlockRenderTable> table)
    : m_table(std::move(table))
{
}

QcBlockLightEngine::Column *QcBlockLightEngine::column(int x, int z)
{
    const QcChunkPos pos{floorChunk(x), floorChunk(z)};
    if (m_cached && pos == m_cachedPos) {
        return m_cached;
    }
    const auto it = m_columns.find(pos);
    m_cachedPos = pos;
    m_cached = it == m_columns.end() ? nullptr : &it->second;
    return m_cached;
}

const QcBlockLightEngine::Column *QcBlockLightEngine::column(int x, int z) const
{
    const auto it = m_columns.find({floorChunk(x), floorChunk(z)});
    return it == m_columns.end() ? nullptr : &it->second;
}

int QcBlockLightEngine::levelIn(const Column &column, int x, int y, int z)
{
    const auto &section = column.levels[std::size_t(y / QcWorld::sectionHeight)];
    return section ? (*section)[std::size_t(QcChunkSection::index(localOf(x), y % QcWorld::sectionHeight, localOf(z)))] : 0;
}

void QcBlockLightEngine::setLevel(Column &column, int x, int y, int z, int level)
{
    const int sectionIndex = y / QcWorld::sectionHeight;
    auto &section = column.levels[std::size_t(sectionIndex)];
    if (!section) {
        if (level == 0) {
            return;
        }
        section = std::make_unique<QcChunkLight::Section>();
        section->fill(0);
    }
    (*section)[std::size_t(QcChunkSection::index(localOf(x), y % QcWorld::sectionHeight, localOf(z)))] = quint8(level);
    const QcChunkPos chunk{floorChunk(x), floorChunk(z)};
    const quint64 key = qcSectionKey(chunk, sectionIndex);
    if (!m_touched.contains(key)) {
        m_touched.insert(key);
        m_touchedRefs.append({chunk, sectionIndex});
    }
}

const QcBlockRenderTable::Entry &QcBlockLightEngine::entryAt(const Column &column, int x, int y, int z) const
{
    return m_table->entry(column.blocks->block(localOf(x), y, localOf(z)));
}

void QcBlockLightEngine::propagateAdd()
{
    for (std::size_t head = 0; head < m_add.size(); ++head) {
        const Cell cell = m_add[head];
        const Column *own = column(cell.x, cell.z);
        if (!own) {
            continue;
        }
        const int light = levelIn(*own, cell.x, cell.y, cell.z);
        ++m_visited;
        if (light <= 1) {
            continue;
        }
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
            const int opacity = entryAt(*target, next.x, next.y, next.z).lightOpacity;
            const int candidate = light - std::max(1, opacity);
            if (opacity < maxLevel && candidate > levelIn(*target, next.x, next.y, next.z)) {
                setLevel(*target, next.x, next.y, next.z, candidate);
                m_add.push_back(next);
            }
        }
    }
    m_add.clear();
}

void QcBlockLightEngine::propagateRemove()
{
    std::vector<Cell> sources;
    for (std::size_t head = 0; head < m_remove.size(); ++head) {
        const auto [cell, old] = m_remove[head];
        ++m_visited;
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
            const int light = levelIn(*target, next.x, next.y, next.z);
            if (light == 0) {
                continue;
            }
            if (light < old) {
                // Possibly lit through the cleared cell: clear it too; what else reaches it comes
                // back with the refill.
                setLevel(*target, next.x, next.y, next.z, 0);
                m_remove.emplace_back(next, light);
                if (entryAt(*target, next.x, next.y, next.z).lightLevel > 0) {
                    sources.push_back(next);
                }
            } else {
                // Lit independently: it refills the cleared cells.
                m_add.push_back(next);
            }
        }
    }
    m_remove.clear();
    for (const Cell &cell : sources) {
        Column *own = column(cell.x, cell.z);
        const int emitted = entryAt(*own, cell.x, cell.y, cell.z).lightLevel;
        if (emitted > levelIn(*own, cell.x, cell.y, cell.z)) {
            setLevel(*own, cell.x, cell.y, cell.z, emitted);
        }
        m_add.push_back(cell);
    }
}

void QcBlockLightEngine::queueBorder(QcChunkPos from, QcBlockFace towards)
{
    const auto it = m_columns.find(from);
    if (it == m_columns.end()) {
        return;
    }
    const Column &source = it->second;
    const QcFaceOffset offset = qcFaceOffset(towards);
    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        if (!source.levels[std::size_t(index)]) {
            continue;
        }
        for (int y = index * QcWorld::sectionHeight; y < (index + 1) * QcWorld::sectionHeight; ++y) {
            for (int along = 0; along < QcWorld::chunkSize; ++along) {
                const int lx = offset.dx != 0 ? (offset.dx > 0 ? last : 0) : along;
                const int lz = offset.dz != 0 ? (offset.dz > 0 ? last : 0) : along;
                const Cell cell{from.x * QcWorld::chunkSize + lx, y, from.z * QcWorld::chunkSize + lz};
                if (levelIn(source, cell.x, y, cell.z) > 0) {
                    m_add.push_back(cell);
                }
            }
        }
    }
}

void QcBlockLightEngine::addChunk(std::shared_ptr<const QcChunk> chunk)
{
    const QcChunkPos pos = chunk->pos();
    m_cached = nullptr;
    if (m_columns.contains(pos)) {
        removeChunk(pos);
    }
    Column &added = m_columns[pos];
    added.blocks = std::move(chunk);
    m_cached = nullptr;
    const auto emits = [this](QcBlockId id) { return m_table->entry(id).lightLevel > 0; };
    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        const QcChunkSection &section = added.blocks->section(index);
        if (std::ranges::none_of(section.palette(), emits)) {
            continue;
        }
        for (int ly = 0; ly < QcWorld::sectionHeight; ++ly) {
            for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
                for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
                    const int emitted = m_table->entry(section.block(lx, ly, lz)).lightLevel;
                    if (emitted > 0) {
                        const Cell cell{pos.x * QcWorld::chunkSize + lx, index * QcWorld::sectionHeight + ly,
                                        pos.z * QcWorld::chunkSize + lz};
                        setLevel(added, cell.x, cell.y, cell.z, emitted);
                        m_add.push_back(cell);
                    }
                }
            }
        }
    }
    // Light of the neighbours flows in over the borders.
    for (const QcBlockFace face : {QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West}) {
        const QcFaceOffset offset = qcFaceOffset(face);
        queueBorder({pos.x + offset.dx, pos.z + offset.dz}, face == QcBlockFace::North   ? QcBlockFace::South
                                                             : face == QcBlockFace::South ? QcBlockFace::North
                                                             : face == QcBlockFace::East  ? QcBlockFace::West
                                                                                          : QcBlockFace::East);
    }
    propagateAdd();
}

void QcBlockLightEngine::removeChunk(QcChunkPos pos)
{
    const auto it = m_columns.find(pos);
    if (it == m_columns.end()) {
        return;
    }
    // The light of the border cells is what flowed (or could have flowed) into the neighbours.
    std::vector<std::pair<Cell, int>> seeds;
    for (const QcBlockFace face : {QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West}) {
        const QcFaceOffset offset = qcFaceOffset(face);
        if (!m_columns.contains({pos.x + offset.dx, pos.z + offset.dz})) {
            continue;
        }
        const Column &removed = it->second;
        for (int index = 0; index < QcWorld::sectionCount; ++index) {
            if (!removed.levels[std::size_t(index)]) {
                continue;
            }
            for (int y = index * QcWorld::sectionHeight; y < (index + 1) * QcWorld::sectionHeight; ++y) {
                for (int along = 0; along < QcWorld::chunkSize; ++along) {
                    const int lx = offset.dx != 0 ? (offset.dx > 0 ? last : 0) : along;
                    const int lz = offset.dz != 0 ? (offset.dz > 0 ? last : 0) : along;
                    const Cell cell{pos.x * QcWorld::chunkSize + lx, y, pos.z * QcWorld::chunkSize + lz};
                    if (const int light = levelIn(removed, cell.x, y, cell.z); light > 0) {
                        seeds.emplace_back(cell, light);
                    }
                }
            }
        }
    }
    m_columns.erase(it);
    m_cached = nullptr;
    m_remove = std::move(seeds);
    propagateRemove();
    propagateAdd();
}

void QcBlockLightEngine::changeBlock(QcBlockPos pos, std::shared_ptr<const QcChunk> chunk)
{
    Column *own = column(pos.x, pos.z);
    if (!own || !pos.isInsideWorldHeight()) {
        return;
    }
    const QcBlockRenderTable::Entry &before = entryAt(*own, pos.x, pos.y, pos.z);
    own->blocks = std::move(chunk);
    const QcBlockRenderTable::Entry &after = entryAt(*own, pos.x, pos.y, pos.z);
    if (before.lightOpacity == after.lightOpacity && before.lightLevel == after.lightLevel) {
        return;
    }
    const Cell cell{pos.x, pos.y, pos.z};
    if (const int old = levelIn(*own, cell.x, cell.y, cell.z); old > 0) {
        setLevel(*own, cell.x, cell.y, cell.z, 0);
        m_remove.emplace_back(cell, old);
        propagateRemove();
    }
    if (after.lightLevel > levelIn(*own, cell.x, cell.y, cell.z)) {
        setLevel(*own, cell.x, cell.y, cell.z, after.lightLevel);
    }
    m_add.push_back(cell);
    // Light of the neighbours may now pass into the cell.
    for (const QcBlockFace face : qcAllBlockFaces) {
        const QcFaceOffset offset = qcFaceOffset(face);
        const Cell next{cell.x + offset.dx, cell.y + offset.dy, cell.z + offset.dz};
        if (next.y >= 0 && next.y < QcWorld::worldHeight) {
            if (const Column *target = column(next.x, next.z); target && levelIn(*target, next.x, next.y, next.z) > 0) {
                m_add.push_back(next);
            }
        }
    }
    propagateAdd();
}

QcBlockLightEngine::Changes QcBlockLightEngine::takeChanges()
{
    Changes changes;
    QSet<quint64> remeshed;
    const auto remesh = [&](QcChunkPos chunk, int section) {
        if (section < 0 || section >= QcWorld::sectionCount) {
            return;
        }
        const quint64 key = qcSectionKey(chunk, section);
        if (!remeshed.contains(key)) {
            remeshed.insert(key);
            changes.remesh.append({chunk, section});
        }
    };
    for (const SectionRef &ref : std::as_const(m_touchedRefs)) {
        const auto it = m_columns.find(ref.chunk);
        if (it == m_columns.end()) {
            continue;
        }
        Column &touched = it->second;
        std::unique_ptr<QcChunkLight::Section> &current = touched.levels[std::size_t(ref.section)];
        std::shared_ptr<const QcChunkLight::Section> &published = touched.published[std::size_t(ref.section)];
        // Neighbouring sections (dx, dy, dz in -1..1) whose meshes read a changed cell.
        std::array<bool, 27> reach{};
        bool changed = false;
        bool lit = false;
        for (int index = 0; index < QcWorld::blocksPerSection; ++index) {
            const int now = current ? (*current)[std::size_t(index)] : 0;
            const int was = published ? (*published)[std::size_t(index)] : 0;
            lit = lit || now > 0;
            if (now == was) {
                continue;
            }
            changed = true;
            const int lx = index % QcWorld::chunkSize;
            const int lz = (index / QcWorld::chunkSize) % QcWorld::chunkSize;
            const int ly = index / (QcWorld::chunkSize * QcWorld::chunkSize);
            for (int dy = ly == 0 ? -1 : 0; dy <= (ly == QcWorld::sectionHeight - 1 ? 1 : 0); ++dy) {
                for (int dz = lz == 0 ? -1 : 0; dz <= (lz == last ? 1 : 0); ++dz) {
                    for (int dx = lx == 0 ? -1 : 0; dx <= (lx == last ? 1 : 0); ++dx) {
                        reach[std::size_t(dy + 1) * 9 + std::size_t(dz + 1) * 3 + std::size_t(dx + 1)] = true;
                    }
                }
            }
        }
        if (!changed) {
            continue;
        }
        if (!lit) {
            current.reset();
            published.reset();
        } else {
            published = std::make_shared<const QcChunkLight::Section>(*current);
        }
        changes.sections.append({ref.chunk, ref.section, published});
        for (int i = 0; i < 27; ++i) {
            if (reach[std::size_t(i)]) {
                remesh({ref.chunk.x + i % 3 - 1, ref.chunk.z + (i / 3) % 3 - 1}, ref.section + i / 9 - 1);
            }
        }
    }
    m_touched.clear();
    m_touchedRefs.clear();
    return changes;
}

QcChunkLight QcBlockLightEngine::snapshot(QcChunkPos pos) const
{
    QcChunkLight light;
    const auto it = m_columns.find(pos);
    if (it != m_columns.end()) {
        light.sections = it->second.published;
    }
    return light;
}

int QcBlockLightEngine::level(QcBlockPos pos) const
{
    const Column *own = column(pos.x, pos.z);
    return own && pos.isInsideWorldHeight() ? levelIn(*own, pos.x, pos.y, pos.z) : 0;
}
