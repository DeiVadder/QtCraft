#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcItemStack.h"
#include "common/world/QcChunk.h"

#include <QHash>
#include <QList>

#include <array>
#include <vector>

// The items cooking on the lit campfires of the loaded chunks (decision E-772): one entry per campfire
// that holds an item, filed by chunk so that they are saved, unloaded and loaded with their chunk
// (QcBlockEntityRecord::Table::Campfires). Kept in step with the blocks by QcServerWorld: a campfire
// whose block goes or stops cooking (put out, broken) loses its entry, and its items spill, to be
// dropped by QcServerBlocks::dropSpills. The rules that fill and cook them are QcServerKitchen.
class QcCampfires
{
public:
    struct Slot
    {
        // At most one item.
        QcItemStack stack;
        // Seconds it has cooked.
        double cooked = 0.0;

        bool operator==(const Slot &) const = default;
    };

    struct Entry
    {
        QcBlockPos pos;
        std::array<Slot, QcCampfireDefinition::maxSlots> places;

        bool isEmpty() const;
        bool operator==(const Entry &) const = default;
    };

    struct Spill
    {
        QcBlockPos pos;
        QList<QcItemStack> stacks;
    };

    explicit QcCampfires(const QcContentRegistry &content);

    // Item slots of a block state that cooks (a lit campfire), 0 for others.
    int slotsOf(QcBlockId block) const { return std::size_t(block) < m_slots.size() ? m_slots[std::size_t(block)] : 0; }

    // The block at `pos` became `after` (QcServerWorld::setBlock): an entry it no longer fits goes and
    // its items spill.
    void noteBlock(QcBlockPos pos, QcBlockId after);
    // Forgets the entries of a chunk without spilling (it left the world or was generated again).
    void forgetChunk(QcChunkPos pos);
    // The entries of a chunk in position order, to be saved; takeChunk() also forgets them.
    QList<Entry> savedOf(QcChunkPos pos) const;
    QList<Entry> takeChunk(QcChunkPos pos);
    // Puts back saved entries where the chunk still has a campfire with that many slots.
    void restore(const QList<Entry> &entries, const QcChunk &chunk);

    // Puts one item into the first free slot of the campfire at `pos` (of `slotCount` slots); false when
    // they are taken.
    bool put(QcBlockPos pos, int slotCount, QcItemId item);
    // Adds `seconds` to the cooking time of every item on the campfire at `pos` (no change for clients).
    void addCookingTime(QcBlockPos pos, double seconds);
    // Replaces the entry at its position (an empty one goes).
    void set(const Entry &entry);
    const Entry *at(QcBlockPos pos) const;
    qsizetype size() const { return m_entries.size(); }
    // Every entry in position order (the cooking steps go in this order).
    QList<QcBlockPos> positions() const;
    QList<QcBlockPos> positionsIn(QcChunkPos pos) const { return m_byChunk.value(pos); }

    // Campfires whose items changed since the last call (clients are told; cooking time is no change).
    [[nodiscard]] QList<QcBlockPos> takeChanges();
    // The items spilled since the last call.
    [[nodiscard]] QList<Spill> takeSpills();
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
