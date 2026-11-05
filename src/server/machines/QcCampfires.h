// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcItemStack.h"
#include "common/world/QcChunk.h"

#include <QHash>
#include <QList>

#include <array>
#include <vector>

/*! \brief The items cooking on the lit campfires of the loaded chunks (decision E-772): one entry
    per campfire that holds an item, filed by chunk so that they are saved, unloaded and loaded with
    their chunk (QcBlockEntityRecord::Table::Campfires).

    Kept in step with the blocks by QcServerWorld: a campfire whose block goes or stops cooking
    (put out, broken) loses its entry, and its items spill, to be dropped by
    QcServerBlocks::dropSpills. The rules that fill and cook them are QcServerKitchen. */
class QcCampfires
{
public:
    /*! \brief One place on a campfire and the item cooking there. */
    struct Slot
    {
        /*! At most one item. */
        QcItemStack stack;
        /*! Seconds it has cooked. */
        double cooked = 0.0;

        bool operator==(const Slot &) const = default;
    };

    /*! \brief A campfire that holds an item. */
    struct Entry
    {
        /*! The position of the campfire block. */
        QcBlockPos pos;
        /*! Its places; those beyond the block's slots stay empty. */
        std::array<Slot, QcCampfireDefinition::maxSlots> places;

        /*! Returns whether no place holds an item. */
        bool isEmpty() const;
        bool operator==(const Entry &) const = default;
    };

    /*! \brief Items that left a campfire with its entry, to be dropped at \c {pos}. \qcfields */
    struct Spill
    {
        QcBlockPos pos;
        QList<QcItemStack> stacks;
    };

    explicit QcCampfires(const QcContentRegistry &content);

    /*! Returns the item slots of \a block when it is a state that cooks (a lit campfire), 0 for
        others. */
    int slotsOf(QcBlockId block) const { return static_cast<std::size_t>(block) < m_slots.size() ? m_slots[static_cast<std::size_t>(block)] : 0; }

    /*! The block at \a pos became \a after (QcServerWorld::setBlock): an entry it no longer fits
        goes and its items spill. */
    void noteBlock(QcBlockPos pos, QcBlockId after);
    /*! Forgets the entries of the chunk at \a pos without spilling (it left the world or was
        generated again). */
    void forgetChunk(QcChunkPos pos);
    /*! Returns the entries of the chunk at \a pos in position order, to be saved. */
    QList<Entry> savedOf(QcChunkPos pos) const;
    /*! Returns the entries of the chunk at \a pos like savedOf() and forgets them. */
    QList<Entry> takeChunk(QcChunkPos pos);
    /*! Puts back the saved \a entries where \a chunk still has a campfire with that many slots. */
    void restore(const QList<Entry> &entries, const QcChunk &chunk);

    /*! Puts one \a item into the first free slot of the campfire at \a pos (of \a slotCount
        slots). Returns \c false when they are taken. */
    bool put(QcBlockPos pos, int slotCount, QcItemId item);
    /*! Adds \a seconds to the cooking time of every item on the campfire at \a pos (no change
        for clients). */
    void addCookingTime(QcBlockPos pos, double seconds);
    /*! Replaces the entry at the position of \a entry with it (an empty one goes). */
    void set(const Entry &entry);
    /*! Returns the entry at \a pos, or null when there is none. */
    const Entry *at(QcBlockPos pos) const;
    /*! Returns the number of entries. */
    qsizetype size() const { return m_entries.size(); }
    /*! Returns the positions of every entry in position order (the cooking steps go in this
        order). */
    QList<QcBlockPos> positions() const;
    /*! Returns the positions of the entries in the chunk at \a pos. */
    QList<QcBlockPos> positionsIn(QcChunkPos pos) const { return m_byChunk.value(pos); }

    /*! Returns the campfires whose items changed since the last call (clients are told; cooking
        time is no change). */
    [[nodiscard]] QList<QcBlockPos> takeChanges();
    /*! Returns the items spilled since the last call. */
    [[nodiscard]] QList<Spill> takeSpills();
    /*! Forgets every entry and spill. */
    void clear();

private:
    void remove(QcBlockPos pos);

    // Slots per block state id.
    std::vector<int> m_slots;
    QHash<QcBlockPos, Entry> m_entries;
    QHash<QcChunkPos, QList<QcBlockPos>> m_byChunk;
    QList<QcBlockPos> m_changes;
    QList<Spill> m_spills;
};
