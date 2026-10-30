// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/world/QcChunk.h"

#include <QHash>
#include <QList>

#include <array>
#include <vector>

/*! \brief Properties of blocks that the server finds by section instead of scanning the world
    (decision E-359).

    Each comes from the block data (QcSectionMarks::traitsOf). */
enum class QcBlockTrait : quint8 {
    /*! Hurts what touches it (lava, magma). */
    ContactDamage,
    /*! Gets random ticks: growing plants, crops, soil. */
    RandomTicks,
    /*! A Signal Dust component other than a hopper (decision E-335; hoppers react only to power,
        which comes from components that are marked themselves, decision E-344). */
    Circuit,
    /*! A pressure plate or tripwire: only bodies in its sections are checked for pressing one. */
    Plate,
    /*! Holds a block entity created with the block: a container or a machine (decision E-357). */
    BlockEntity,
    /*! Fire (decision E-392): a chunk that arrives is searched for fire blocks only in these
        sections. */
    Fire,
    /*! A monster spawner (decision E-520): a chunk that arrives is searched for spawners only
        here. */
    Spawner,
    /*! A rail: a chunk that arrives without one gets no minecart from its generator (decision
        E-570). */
    Rail,
};
/*! The number of block traits. */
inline constexpr int qcBlockTraitCount = 8;

/*! \brief Per loaded chunk and block trait, a bit for each section that may hold a block with that
    trait.

    A bit is set when its chunk arrives or such a block is placed and only cleared when the chunk
    is noted again or forgotten, so it means "maybe": callers still look at the blocks. Chunks
    without any marked section are missing, so a tick over the marked sections costs nothing in a
    world without such blocks. One object marks every trait in one pass over the palettes and
    forgets a chunk with one call. */
class QcSectionMarks
{
public:
    /*! One bit per QcBlockTrait. */
    using Traits = quint8;

    /*! Returns the traits of \a block from its data. */
    static Traits traitsOf(const QcBlockDefinition &block);
    /*! Returns the bit of \a trait. */
    static constexpr Traits bit(QcBlockTrait trait) { return static_cast<Traits>(1u << static_cast<unsigned>(trait)); }

    QcSectionMarks() = default;
    /*! \a traits is indexed by block state id. */
    explicit QcSectionMarks(std::vector<Traits> traits);

    /*! Returns the traits of the state \a block. */
    Traits traitsOf(QcBlockId block) const
    {
        return static_cast<std::size_t>(block) < m_traits.size() ? m_traits[static_cast<std::size_t>(block)] : static_cast<Traits>(0);
    }
    /*! Returns whether the state \a block has \a trait. */
    bool isMarked(QcBlockId block, QcBlockTrait trait) const { return (traitsOf(block) & bit(trait)) != 0; }

    /*! Replaces the bits of \a chunk at \a pos that entered the world from its section palettes. */
    void noteChunk(QcChunkPos pos, const QcChunk &chunk);
    /*! Sets the bits of the section \a block was placed in at \a pos for its traits. */
    void noteBlock(QcBlockPos pos, QcBlockId block);
    /*! Forgets the bits of the chunk \a pos that left the world. */
    void forgetChunk(QcChunkPos pos);

    /*! Returns whether no loaded chunk may hold a block with \a trait. */
    bool isEmpty(QcBlockTrait trait) const { return m_chunkCount[static_cast<std::size_t>(trait)] == 0; }
    /*! Returns the number of chunks with sections marked for \a trait. */
    qsizetype chunkCount(QcBlockTrait trait) const { return m_chunkCount[static_cast<std::size_t>(trait)]; }
    /*! Returns the bits of the chunk \a pos for \a trait (bit n: section n), 0 for a chunk
        without such sections. */
    quint32 sectionsOf(QcChunkPos pos, QcBlockTrait trait) const;
    /*! Returns the chunks with sections marked for \a trait, sorted by x, then z: an order that
        does not depend on the hash of the process (QcPlantGrowth draws its random cells in it).
        Sorted again only after chunks gained or lost the trait, not on every call. The returned
        reference is only valid until the next noteChunk, noteBlock or forgetChunk (copy it to
        hold it across changes). */
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
