// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/world/QcChunk.h"

#include <QHash>
#include <QList>

#include <array>
#include <vector>

// Properties of blocks that the server finds by section instead of scanning the world (decision
// E-359). Each comes from the block data (QcSectionMarks::traitsOf).
enum class QcBlockTrait : quint8 {
    // Hurts what touches it (lava, magma).
    ContactDamage,
    // Gets random ticks: growing plants, crops, soil.
    RandomTicks,
    // A Signal Dust component other than a hopper (decision E-335; hoppers react only to power,
    // which comes from components that are marked themselves, decision E-344).
    Circuit,
    // A pressure plate or tripwire: only bodies in its sections are checked for pressing one.
    Plate,
    // Holds a block entity created with the block: a container or a machine (decision E-357).
    BlockEntity,
    // Fire (decision E-392): a chunk that arrives is searched for fire blocks only in these sections.
    Fire,
    // A monster spawner (decision E-520): a chunk that arrives is searched for spawners only here.
    Spawner,
    // A rail: a chunk that arrives without one gets no minecart from its generator (decision E-570).
    Rail,
};
inline constexpr int qcBlockTraitCount = 8;

// Per loaded chunk and block trait, a bit for each section that may hold a block with that trait.
// A bit is set when its chunk arrives or such a block is placed and only cleared when the chunk is
// noted again or forgotten, so it means "maybe": callers still look at the blocks. Chunks without
// any marked section are missing, so a tick over the marked sections costs nothing in a world
// without such blocks. One object marks every trait in one pass over the palettes and forgets a
// chunk with one call.
class QcSectionMarks
{
public:
    // One bit per QcBlockTrait.
    using Traits = quint8;

    static Traits traitsOf(const QcBlockDefinition &block);
    static constexpr Traits bit(QcBlockTrait trait) { return static_cast<Traits>(1u << static_cast<unsigned>(trait)); }

    QcSectionMarks() = default;
    // `traits` is indexed by block state id.
    explicit QcSectionMarks(std::vector<Traits> traits);

    Traits traitsOf(QcBlockId block) const
    {
        return static_cast<std::size_t>(block) < m_traits.size() ? m_traits[static_cast<std::size_t>(block)] : static_cast<Traits>(0);
    }
    bool isMarked(QcBlockId block, QcBlockTrait trait) const { return (traitsOf(block) & bit(trait)) != 0; }

    // Replaces the bits of a chunk that entered the world from its section palettes.
    void noteChunk(QcChunkPos pos, const QcChunk &chunk);
    // Sets the bits of the section `block` was placed in for its traits.
    void noteBlock(QcBlockPos pos, QcBlockId block);
    void forgetChunk(QcChunkPos pos);

    // Whether no loaded chunk may hold a block with the trait.
    bool isEmpty(QcBlockTrait trait) const { return m_chunkCount[static_cast<std::size_t>(trait)] == 0; }
    qsizetype chunkCount(QcBlockTrait trait) const { return m_chunkCount[static_cast<std::size_t>(trait)]; }
    // The bits of a chunk for the trait (bit n: section n), 0 for a chunk without such sections.
    quint32 sectionsOf(QcChunkPos pos, QcBlockTrait trait) const;
    // The chunks with sections marked for the trait, sorted by x, then z: an order that does not
    // depend on the hash of the process (QcPlantGrowth draws its random cells in it). Sorted again
    // only after chunks gained or lost the trait, not on every call. The returned reference is only
    // valid until the next noteChunk, noteBlock or forgetChunk (copy it to hold it across changes).
    const QList<QcChunkPos> &orderedChunks(QcBlockTrait trait) const;

private:
    using Bits = std::array<quint32, qcBlockTraitCount>;

    // Keeps the counts and orders in step with a chunk whose bits went from `before` to `after`.
    void noteBits(const Bits &before, const Bits &after);

    std::vector<Traits> m_traits;
    QHash<QcChunkPos, Bits> m_chunks;
    std::array<qsizetype, qcBlockTraitCount> m_chunkCount{};
    mutable std::array<QList<QcChunkPos>, qcBlockTraitCount> m_order;
    mutable std::array<bool, qcBlockTraitCount> m_orderStale{};
};
