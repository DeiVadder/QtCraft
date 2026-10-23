#include "clientcore/light/QcBlockLightEngine.h"

#include "common/core/QcSectionKey.h"

namespace {

constexpr int last = QcWorld::chunkSize - 1;
using Channel = QcLightEngine::Channel;

// Neighbouring sections (dx, dy, dz in -1..1, index (dy + 1) * 9 + (dz + 1) * 3 + dx + 1) whose
// meshes read a changed cell.
struct SectionReach
{
    std::array<bool, 27> sections{};

    // Marks the sections next to the cell `index` of a section (and the section itself) as reached.
    void mark(int index)
    {
        const int lx = index % QcWorld::chunkSize;
        const int lz = (index / QcWorld::chunkSize) % QcWorld::chunkSize;
        const int ly = index / (QcWorld::chunkSize * QcWorld::chunkSize);
        for (int dy = ly == 0 ? -1 : 0; dy <= (ly == QcWorld::sectionHeight - 1 ? 1 : 0); ++dy) {
            for (int dz = lz == 0 ? -1 : 0; dz <= (lz == last ? 1 : 0); ++dz) {
                for (int dx = lx == 0 ? -1 : 0; dx <= (lx == last ? 1 : 0); ++dx) {
                    sections[static_cast<std::size_t>(dy + 1) * 9 + static_cast<std::size_t>(dz + 1) * 3 + static_cast<std::size_t>(dx + 1)] = true;
                }
            }
        }
    }
};

// What a section's current levels changed against its published ones.
struct SectionDiff
{
    SectionReach reach;
    bool changed = false;
    bool lit = false;
};

// `current` null: every cell has the level `uniform`. `published` null: dark.
SectionDiff diffSection(const QcLightNibbles *current, int uniform, const QcLightNibbles *published)
{
    // Locals rather than fields of the result keep the per-cell loop in registers.
    SectionReach reach;
    bool changed = false;
    bool lit = false;

    for (int index = 0; index < QcWorld::blocksPerSection; ++index) {
        const int now = current ? current->level(index) : uniform;
        const int was = published ? published->level(index) : 0;
        lit = lit || now > 0;
        if (now == was) {
            continue;
        }
        changed = true;
        reach.mark(index);
    }

    return {reach, changed, lit};
}

// Sections to mesh again, each listed once.
class RemeshList
{
public:
    // Adds the section unless it is outside the world or already listed.
    void add(QcChunkPos chunk, int section)
    {
        if (section < 0 || section >= QcWorld::sectionCount) {
            return;
        }
        const quint64 key = qcSectionKey(chunk, section);
        if (!m_listed.contains(key)) {
            m_listed.insert(key);
            m_sections.append({chunk, section});
        }
    }

    QList<QcBlockLightEngine::SectionRef> take() { return std::move(m_sections); }

private:
    QList<QcBlockLightEngine::SectionRef> m_sections;
    QSet<quint64> m_listed;
};

} // namespace

QcBlockLightEngine::QcBlockLightEngine(const std::shared_ptr<const QcBlockRenderTable> &table)
    : m_engine(table->lightTable(), {.sky = false, .trackChanges = true})
{
}

void QcBlockLightEngine::addChunk(std::shared_ptr<const QcChunk> chunk)
{
    const QcChunkPos pos = chunk->pos();
    m_published.erase(pos);
    m_engine.addChunk(std::move(chunk));
    m_published[pos];
}

void QcBlockLightEngine::removeChunk(QcChunkPos pos)
{
    m_engine.removeChunk(pos);
    m_published.erase(pos);
}

void QcBlockLightEngine::changeBlock(QcBlockPos pos, std::shared_ptr<const QcChunk> chunk)
{
    m_engine.changeBlock(pos, std::move(chunk));
}

std::shared_ptr<const QcChunkLight::Section> QcBlockLightEngine::current(QcChunkPos pos, int section) const
{
    if (const QcLightNibbles *cells = m_engine.section(Channel::Block, pos, section)) {
        return std::make_shared<const QcChunkLight::Section>(*cells);
    }
    const int uniform = m_engine.uniformLevel(Channel::Block, pos, section);
    return uniform > 0 ? std::make_shared<const QcChunkLight::Section>(uniform) : nullptr;
}

QcBlockLightEngine::Changes QcBlockLightEngine::takeChanges()
{
    Changes changes;
    RemeshList remesh;
    const QList<QcLightEngine::SectionRef> touched = m_engine.takeTouched(Channel::Block);
    for (const QcLightEngine::SectionRef &ref : touched) {
        const auto it = m_published.find(ref.chunk);
        if (it == m_published.end()) {
            continue;
        }
        std::shared_ptr<const QcChunkLight::Section> &published = it->second[static_cast<std::size_t>(ref.section)];
        const SectionDiff diff = diffSection(m_engine.section(Channel::Block, ref.chunk, ref.section),
                                             m_engine.uniformLevel(Channel::Block, ref.chunk, ref.section), published.get());
        if (!diff.changed) {
            continue;
        }

        if (!diff.lit) {
            m_engine.compact(Channel::Block, ref.chunk, ref.section);
        }
        published = diff.lit ? current(ref.chunk, ref.section) : nullptr;
        changes.sections.append({ref.chunk, ref.section, published});
        for (int i = 0; i < 27; ++i) {
            if (diff.reach.sections[static_cast<std::size_t>(i)]) {
                remesh.add({ref.chunk.x + i % 3 - 1, ref.chunk.z + (i / 3) % 3 - 1}, ref.section + i / 9 - 1);
            }
        }
    }
    changes.remesh = remesh.take();
    return changes;
}

QcChunkLight QcBlockLightEngine::snapshot(QcChunkPos pos) const
{
    QcChunkLight light;
    if (const auto it = m_published.find(pos); it != m_published.end()) {
        light.sections = it->second;
    }
    return light;
}
