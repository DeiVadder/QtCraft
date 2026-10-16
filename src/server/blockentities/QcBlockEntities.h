#pragma once

#include "QcChunk.h"
#include "QcContentRegistry.h"
#include "QcItemStack.h"
#include "QcSignalSchedule.h"

#include <QHash>
#include <QList>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <vector>

// State that belongs to single blocks of the world (decision E-357), filed by chunk so that it moves,
// drops, is forgotten, saved and loaded with its chunk: the contents of containers (chests, barrels,
// hoppers with their cooldown, dispensers) and machines (furnaces), the timers of growing saplings,
// the memory of circuit components (torches burning out, openers last seen powered, pistons in the
// middle of extending) and the scheduled ticks of components. The kind of an entry comes from the
// block data: a block with `container` has a container, one with `machine` a machine, one with
// `sapling` may have a timer.
//
// QcServerWorld keeps it in step with the blocks: a block set anywhere runs placed(), which drops
// what no longer fits the new block (contents spill, to be dropped as items by the caller) and
// creates the containers and machines of the new block; a chunk entering the world gets the
// containers and machines of its blocks; a chunk generated again forgets everything of the old
// one. Pistons move every entry of a cell with relocate().
class QcBlockEntities
{
public:
    struct Container
    {
        QcBlockPos pos;
        QList<QcItemStack> stacks;
        // A Signal Hopper rests until this tick after moving an item (decision E-344); 0: never moved.
        quint64 readyAt = 0;
    };

    struct Machine
    {
        static constexpr int inputSlot = 0;
        static constexpr int fuelSlot = 1;
        static constexpr int outputSlot = 2;
        static constexpr int slotCount = 3;

        QcBlockPos pos;
        std::array<QcItemStack, slotCount> stacks;
        // Seconds left of the current fuel item and its full burning time.
        double burnRemaining = 0.0;
        double burnSeconds = 0.0;
        // Seconds of work done on the current input item (at speed 1), and that item; another
        // input starts over.
        double progress = 0.0;
        QcItemId progressItem = qcNoItem;
        // Experience of the items smelted since a player last took the output (decision E-398), at
        // most maxExperience; it comes out as orbs when a player takes from the output slot.
        double experience = 0.0;
        static constexpr double maxExperience = 100000.0;

        // Nothing in it and nothing burning or in progress: nothing to simulate (gathered
        // experience waits without a step).
        bool isCold() const;
        // Cold and without gathered experience: the same as a new one (removed, not saved).
        bool isIdle() const;
    };

    struct Sapling
    {
        QcBlockPos pos;
        // Seconds of loaded time until it tries to grow.
        double remaining = 0.0;
        // Saplings tick in the order they were planted.
        quint64 order = 0;
    };

    // What circuit components remember at their position (decision E-335).
    struct Memory
    {
        // Ticks at which a torch went out lately (for burning out); not saved.
        QList<quint64> torchOffTicks;
        // A burnt-out torch may light again from this tick on; 0: not burnt out.
        quint64 burntOutUntil = 0;
        // An opener (the lower half of a door) that was powered when last looked at: it only
        // switches when that changes, so players can still open and close it by hand.
        bool powered = false;
        // A piston whose extension started at this tick: retracting before it ends lets a sticky
        // piston drop its block (block spitting).
        std::optional<quint64> extendingSince;

        bool isEmpty() const { return torchOffTicks.isEmpty() && burntOutUntil == 0 && !powered && !extendingSince; }
    };

    // Everything of one chunk (a copy, see chunk()).
    struct Chunk
    {
        QHash<QcBlockPos, Container> containers;
        QHash<QcBlockPos, Machine> machines;
        QHash<QcBlockPos, Sapling> saplings;
        QHash<QcBlockPos, Memory> memories;

        bool isEmpty() const { return containers.isEmpty() && machines.isEmpty() && saplings.isEmpty() && memories.isEmpty(); }
    };

    // Contents that left the world with their block, to be dropped as items at `pos`, and the
    // experience a machine had gathered, to be dropped as orbs (decision E-398).
    struct Spill
    {
        QcBlockPos pos;
        QList<QcItemStack> stacks;
        double experience = 0.0;
    };

    // Entries of a kind in the whole world at most (QcServerLimits): placing beyond is refused,
    // created entries beyond are left out.
    struct Limits
    {
        qsizetype containers = 16384;
        qsizetype machines = 4096;
        qsizetype saplings = 4096;
    };

    explicit QcBlockEntities(std::shared_ptr<const QcContentRegistry> content);

    const QcContentRegistry &content() const { return *m_content; }
    void setLimits(const Limits &limits) { m_limits = limits; }
    const Limits &limits() const { return m_limits; }

    // What the block data says a block holds.
    int containerSlots(QcBlockId block) const;
    const QcMachineDefinition *machineOf(QcBlockId block) const;
    bool isSapling(QcBlockId block) const;

    // The block at `pos` changed from `before` to `after` (QcServerWorld::setBlock): spills the
    // contents of a machine that is no machine any more and of a container that is none or smaller,
    // creates the container or machine of the new block, ends a sapling's timer and forgets the
    // memory that does not fit the new kind of component. Scheduled ticks stay: they look at
    // whatever block is there when they run.
    void placed(QcBlockPos pos, QcBlockId before, QcBlockId after);
    // A chunk entered the world: reconcile() and the missing containers and machines of the blocks
    // in its marked sections.
    void chunkArrived(const QcChunk &chunk, quint32 sections);
    // Fits what is kept for a chunk to its blocks (a mod took a block away, a save from another
    // version): as placed() would for each entry.
    void reconcile(const QcChunk &chunk);
    // Moves everything of a cell to another (a piston pushed its block, decision E-337), entry by
    // entry unless `to` has one of that kind already.
    void relocate(QcBlockPos from, QcBlockPos to);
    // Forgets everything of a chunk without spilling (it was generated again).
    void forgetChunk(QcChunkPos chunk);
    // The contents spilled since the last call, oldest first.
    QList<Spill> takeSpills();

    Container *container(QcBlockPos pos);
    const Container *container(QcBlockPos pos) const;
    Machine *machine(QcBlockPos pos);
    const Machine *machine(QcBlockPos pos) const;
    // Creates the container or machine of `block` at `pos` unless there is one; false when the
    // block holds none or the limit of its kind is reached.
    bool ensure(QcBlockPos pos, QcBlockId block);
    // Takes a container or machine with its contents for setting up and loading; replaces one at
    // the same position. The limits refuse only placing: what a save holds always comes back.
    bool putContainer(const Container &container);
    bool putMachine(const Machine &machine);
    // Calls `visit(Machine &)` for every machine (QcMachineRules::tick).
    template<typename Visit>
    void forEachMachine(Visit visit)
    {
        // NOLINTNEXTLINE(misc-const-correctness): `visit` changes the machine.
        for (Machine &machine : m_machines) {
            visit(machine);
        }
    }
    qsizetype containerCount() const { return m_containers.size(); }
    qsizetype machineCount() const { return m_machines.size(); }
    bool isContainerTableFull() const { return m_containers.size() >= m_limits.containers; }
    bool isMachineTableFull() const { return m_machines.size() >= m_limits.machines; }

    // Starts the timer of a sapling block just placed; false (with a warning) when too many grow.
    bool plantSapling(QcBlockPos pos, double remaining);
    const Sapling *sapling(QcBlockPos pos) const;
    void setSaplingRemaining(QcBlockPos pos, double remaining);
    void removeSapling(QcBlockPos pos);
    // All growing saplings in the order they were planted; their orders, and a sapling by its order
    // (null once it is gone; valid until it is removed).
    QList<Sapling> saplingsInOrder() const;
    QList<quint64> saplingOrders() const;
    Sapling *saplingByOrder(quint64 order);
    qsizetype saplingCount() const { return qsizetype(m_saplings.size()); }
    // Takes a sapling from a save with its order (also beyond the limit); false when its position
    // or order is taken.
    bool putSapling(Sapling sapling);

    // What growSaplings-like ticks tell tickSaplings about one sapling.
    enum class SaplingStep : quint8 { Keep, Remove, Grew };
    // Calls `visit(Sapling &)` for every sapling in planting order. Keep: next one; Remove: the
    // timer ends; Grew: the timer ends and the walk goes on after this sapling, because growing
    // changed the world (and may have ended other timers).
    template<typename Visit>
    void tickSaplings(Visit visit)
    {
        for (auto it = m_saplings.begin(); it != m_saplings.end();) {
            const quint64 order = it->first;
            const QcBlockPos pos = it->second.pos;
            switch (visit(it->second)) {
            case SaplingStep::Keep:
                ++it;
                break;
            case SaplingStep::Remove:
                ++it;
                removeSapling(pos);
                break;
            case SaplingStep::Grew:
                removeSapling(pos);
                it = m_saplings.upper_bound(order);
                break;
            }
        }
    }

    // The memory of the component at `pos`, if it has any; memoryAt() creates it.
    const Memory *memory(QcBlockPos pos) const;
    Memory &memoryAt(QcBlockPos pos);
    // Drops memories that ran out: torches that have not gone out within `torchWindow` ticks, ended
    // burnouts and extensions that lasted `extensionTicks`; empty ones go. Ended hopper cooldowns
    // are cleared.
    void prune(quint64 now, quint64 torchWindow, quint64 extensionTicks);

    // Scheduled ticks of circuit components, one per position.
    QcSignalSchedule &schedule() { return m_schedule; }
    const QcSignalSchedule &schedule() const { return m_schedule; }

    // The positions of a chunk's entries by kind: the entries live in one table per kind, so lookups
    // and ticks are one hash access, and everything of a chunk is found without a scan.
    struct ChunkIndex
    {
        QList<QcBlockPos> containers;
        QList<QcBlockPos> machines;
        QList<QcBlockPos> saplings;
        QList<QcBlockPos> memories;

        bool isEmpty() const { return containers.isEmpty() && machines.isEmpty() && saplings.isEmpty() && memories.isEmpty(); }
    };

    // The chunks with entries (scheduled ticks are kept by the schedule), the positions of one
    // chunk's entries (null for none; valid until the next change) and a copy of what one holds
    // (tests and checks).
    QList<QcChunkPos> chunks() const { return m_byChunk.keys(); }
    const ChunkIndex *chunkIndex(QcChunkPos pos) const;
    [[nodiscard]] std::optional<Chunk> chunk(QcChunkPos pos) const;
    // A chunk left the world (QcServerWorld::unloadChunk): containers that hold nothing and rest
    // and machines that are idle go; they come back with their blocks when the chunk arrives.
    void chunkLeft(QcChunkPos pos);
    void clear();

private:
    // Files `pos` under its chunk in the list `list` of the index, or takes it out again.
    void index(QcBlockPos pos, QList<QcBlockPos> ChunkIndex::*list);
    void unindex(QcBlockPos pos, QList<QcBlockPos> ChunkIndex::*list);
    void removeContainer(QcBlockPos pos);
    void removeMachine(QcBlockPos pos);
    void removeMemory(QcBlockPos pos);
    // The missing containers and machines of the blocks of one section of a chunk.
    void ensureSection(const QcChunk &chunk, int sectionIndex);
    void placedContainer(QcBlockPos pos, QcBlockId after);
    void placedMachine(QcBlockPos pos, QcBlockId after);
    void placedMemory(QcBlockPos pos, QcBlockId before, QcBlockId after);
    // Clears what the memory keeps that does not fit a component of `kind`; true when nothing is left.
    static bool forgetUnfitting(Memory &memory, QcSignalKind kind);
    QcSignalKind kindOf(QcBlockId block) const;
    // A block state that holds no entry of any kind (no container, machine or sapling).
    bool isPlain(QcBlockId block) const { return std::size_t(block) < m_plain.size() && m_plain[std::size_t(block)]; }
    void spill(QcBlockPos pos, QList<QcItemStack> stacks, double experience = 0.0);

    std::shared_ptr<const QcContentRegistry> m_content;
    Limits m_limits;
    // isPlain() per block state id.
    std::vector<bool> m_plain;
    QHash<QcBlockPos, Container> m_containers;
    QHash<QcBlockPos, Machine> m_machines;
    QHash<QcBlockPos, Memory> m_memories;
    // Saplings by the order they were planted, and their orders by position.
    std::map<quint64, Sapling> m_saplings;
    QHash<QcBlockPos, quint64> m_saplingAt;
    quint64 m_nextSaplingOrder = 0;
    QHash<QcChunkPos, ChunkIndex> m_byChunk;
    QcSignalSchedule m_schedule;
    QList<Spill> m_spills;
};
