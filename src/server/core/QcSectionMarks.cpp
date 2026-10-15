#include "QcSectionMarks.h"

#include <algorithm>
#include <utility>

static_assert(QcWorld::sectionCount <= 32, "one bit per section in a quint32");
static_assert(qcBlockTraitCount <= 8, "one bit per trait in QcSectionMarks::Traits");

QcSectionMarks::Traits QcSectionMarks::traitsOf(const QcBlockDefinition &block)
{
    Traits traits = 0;
    // Burning blocks (`setsAlight`) are looked for with the damaging ones (QcContactDamage).
    if (block.contactDamage > 0.0 || block.fire.setsAlightSeconds > 0) {
        traits |= bit(QcBlockTrait::ContactDamage);
    }
    if (block.hasRandomTicks()) {
        traits |= bit(QcBlockTrait::RandomTicks);
    }
    if (block.signal.isComponent() && block.signal.kind != QcSignalKind::Hopper) {
        traits |= bit(QcBlockTrait::Circuit);
    }
    if (block.signal.kind == QcSignalKind::Plate) {
        traits |= bit(QcBlockTrait::Plate);
    }
    if (block.containerSlots > 0 || block.machine.isMachine()) {
        traits |= bit(QcBlockTrait::BlockEntity);
    }
    if (block.fire.isFire) {
        traits |= bit(QcBlockTrait::Fire);
    }
    return traits;
}

QcSectionMarks::QcSectionMarks(std::vector<Traits> traits)
    : m_traits(std::move(traits))
{
}

quint32 QcSectionMarks::sectionsOf(QcChunkPos pos, QcBlockTrait trait) const
{
    const auto it = m_chunks.constFind(pos);
    return it == m_chunks.constEnd() ? 0u : (*it)[std::size_t(trait)];
}

void QcSectionMarks::noteBits(const Bits &before, const Bits &after)
{
    for (std::size_t trait = 0; trait < std::size_t(qcBlockTraitCount); ++trait) {
        if ((before[trait] == 0) != (after[trait] == 0)) {
            m_chunkCount[trait] += after[trait] != 0 ? 1 : -1;
            m_orderStale[trait] = true;
        }
    }
}

void QcSectionMarks::noteChunk(QcChunkPos pos, const QcChunk &chunk)
{
    Bits after{};
    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        Traits traits = 0;
        for (const QcBlockId id : chunk.section(index).palette()) {
            traits |= traitsOf(id);
        }
        for (std::size_t trait = 0; trait < std::size_t(qcBlockTraitCount); ++trait) {
            if ((traits & (1u << trait)) != 0) {
                after[trait] |= 1u << index;
            }
        }
    }

    const Bits before = m_chunks.value(pos, Bits{});
    if (std::ranges::any_of(after, [](quint32 sections) { return sections != 0; })) {
        m_chunks.insert(pos, after);
    } else {
        m_chunks.remove(pos);
    }
    noteBits(before, after);
}

void QcSectionMarks::noteBlock(QcBlockPos pos, QcBlockId block)
{
    const Traits traits = traitsOf(block);
    if (traits == 0) {
        return;
    }

    Bits &bits = m_chunks[pos.chunk()];
    const Bits before = bits;
    for (std::size_t trait = 0; trait < std::size_t(qcBlockTraitCount); ++trait) {
        if ((traits & (1u << trait)) != 0) {
            bits[trait] |= 1u << (pos.y / QcWorld::sectionHeight);
        }
    }
    noteBits(before, bits);
}

void QcSectionMarks::forgetChunk(QcChunkPos pos)
{
    const auto it = m_chunks.constFind(pos);
    if (it == m_chunks.constEnd()) {
        return;
    }
    const Bits before = *it;
    m_chunks.erase(it);
    noteBits(before, Bits{});
}

const QList<QcChunkPos> &QcSectionMarks::orderedChunks(QcBlockTrait trait) const
{
    const auto index = std::size_t(trait);
    if (m_orderStale[index]) {
        QList<QcChunkPos> &order = m_order[index];
        order.clear();
        for (auto it = m_chunks.constBegin(); it != m_chunks.constEnd(); ++it) {
            if ((*it)[index] != 0) {
                order.append(it.key());
            }
        }
        std::ranges::sort(order, [](QcChunkPos a, QcChunkPos b) { return std::pair{a.x, a.z} < std::pair{b.x, b.z}; });
        m_orderStale[index] = false;
    }
    return m_order[index];
}
