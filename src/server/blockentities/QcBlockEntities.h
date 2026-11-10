// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcItemStack.h"
#include "common/world/QcChunk.h"
#include "server/signal/QcSignalSchedule.h"

#include <QHash>
#include <QList>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <vector>

/*! \brief State that belongs to single blocks of the world (decision E-357), filed by chunk so that
    it moves, drops, is forgotten, saved and loaded with its chunk: the contents of containers
    (chests, barrels, hoppers with their cooldown, dispensers) and machines (furnaces), the timers
    of growing saplings, the memory of circuit components (torches burning out, openers last seen
    powered, pistons in the middle of extending) and the scheduled ticks of components.

    The kind of an entry comes from the block data: a block with \c {container} has a container, one
    with \c {machine} a machine, one with \c {sapling} may have a timer.

    QcServerWorld keeps it in step with the blocks: a block set anywhere runs placed(), which
    drops what no longer fits the new block (contents spill, to be dropped as items by the
    caller) and creates the containers and machines of the new block; a chunk entering the world
    gets the containers and machines of its blocks; a chunk generated again forgets everything of
    the old one. Pistons move every entry of a cell with relocate(). */
class QcBlockEntities
{
public:
    /*! \brief The contents of a container block (chest, barrel, hopper, dispenser). */
    struct Container
    {
        /*! The position of the block. */
        QcBlockPos pos;
        /*! The item stacks in its slots, one per slot. */
        QList<QcItemStack> stacks;
        /*! A Signal Hopper rests until this tick after moving an item (decision E-344); 0: never
            moved. */
        quint64 readyAt = 0;
        /*! The custom name of the item it was placed from (decision E-873), the title of its
            screen and the name of the item it drops; empty for none. */
        QString name = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free

        /*! Returns whether something lies in one of its slots. */
        bool isFilled() const;
        /*! Returns whether it holds nothing, has no cooldown and no name: the same as a new one
            (not saved, forgotten with its chunk). */
        bool isPlain() const;
    };

    /*! \brief The state of a machine block: its slots, fuel, work and experience.

        A furnace has the three slots below. A brewing stand (decision E-1063) has two more: its
        input is the ingredient, its fuel the fuel, and the output and the two slots after it
        hold the bottles; \c {burnRemaining} and \c {burnSeconds} count brews instead of seconds,
        \c {progress} is the time of the running brew and \c {progressItem} its ingredient. */
    struct Machine
    {
        /*! The indexes of the input, fuel and output slot and the number of slots. \qcshared */
        static constexpr int inputSlot = 0;
        static constexpr int fuelSlot = 1;
        static constexpr int outputSlot = 2;
        static constexpr int slotCount = 3;

        /*! A stand's filled bottle slots, a bit each, as its block shows them; -1 before the
            first look (QcServerMachines). Not saved. */
        int shownBottles = -1;
        /*! The position of the block. */
        QcBlockPos pos;
        /*! The item stacks in its slots, indexed by inputSlot, fuelSlot and outputSlot, as many as
            the block's machine has (QcMachineDefinition::slotCount). */
        QList<QcItemStack> stacks = QList<QcItemStack>(slotCount);
        /*! Seconds left of the current fuel item and its full burning time. \qcshared */
        double burnRemaining = 0.0;
        double burnSeconds = 0.0;
        /*! Seconds of work done on the current input item (at speed 1), and that item; another
            input starts over. \qcshared */
        double progress = 0.0;
        QcItemId progressItem = qcNoItem;
        /*! Experience of the items smelted since a player last took the output (decision E-398),
            at most maxExperience; it comes out as orbs when a player takes from the output slot.
            \qcshared */
        double experience = 0.0;
        static constexpr double maxExperience = 100000.0;
        /*! The custom name of the item it was placed from (decision E-873); empty for none. */
        QString name = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free

        /*! Returns whether nothing is in it and nothing burning or in progress: nothing to simulate
            (gathered experience waits without a step). */
        bool isCold() const;
        /*! Returns whether it is cold, without gathered experience and without a name: the same as
            a new one (removed, not saved). */
        bool isIdle() const;
    };

    /*! \brief The growth timer of a sapling block. */
    struct Sapling
    {
        /*! The position of the block. */
        QcBlockPos pos;
        /*! Seconds of loaded time until it tries to grow. */
        double remaining = 0.0;
        /*! Saplings tick in the order they were planted. */
        quint64 order = 0;
    };

    /*! \brief What circuit components remember at their position (decision E-335). */
    struct Memory
    {
        /*! Ticks at which a torch went out lately (for burning out); not saved. */
        QList<quint64> torchOffTicks;
        /*! A burnt-out torch may light again from this tick on; 0: not burnt out. */
        quint64 burntOutUntil = 0;
        /*! An opener (the lower half of a door) that was powered when last looked at: it only
            switches when that changes, so players can still open and close it by hand. */
        bool powered = false;
        /*! A piston whose extension started at this tick: retracting before it ends lets a
            sticky piston drop its block (block spitting). */
        std::optional<quint64> extendingSince;

        /*! Returns whether it remembers nothing. */
        bool isEmpty() const { return torchOffTicks.isEmpty() && burntOutUntil == 0 && !powered && !extendingSince; }
        /*! Forgets what does not belong to a component of \a kind. Returns whether nothing is
            left. */
        bool forgetUnfitting(QcSignalKind kind);
    };

    /*! \brief Everything of one chunk (a copy, see chunk()): its containers, machines, saplings
        and memories by position. \qcfields */
    struct Chunk
    {
        QHash<QcBlockPos, Container> containers;
        QHash<QcBlockPos, Machine> machines;
        QHash<QcBlockPos, Sapling> saplings;
        QHash<QcBlockPos, Memory> memories;

        /*! Returns whether the chunk holds no entry. */
        bool isEmpty() const { return containers.isEmpty() && machines.isEmpty() && saplings.isEmpty() && memories.isEmpty(); }
    };

    /*! \brief Contents that left the world with their block, to be dropped as items at \c {pos},
        and the experience a machine had gathered, to be dropped as orbs (decision E-398).
        \qcfields */
    struct Spill
    {
        QcBlockPos pos;
        QList<QcItemStack> stacks;
        double experience = 0.0;
    };

    /*! \brief Entries of a kind in the whole world at most (QcServerLimits): placing beyond is
        refused, created entries beyond are left out. \qcfields */
    struct Limits
    {
        qsizetype containers = 16384;
        qsizetype machines = 4096;
        qsizetype saplings = 4096;
    };

    explicit QcBlockEntities(std::shared_ptr<const QcContentRegistry> content);

    /*! Returns the content registry the block data comes from. */
    const QcContentRegistry &content() const { return *m_content; }
    /*! Sets the \a limits of the tables. */
    void setLimits(const Limits &limits) { m_limits = limits; }
    /*! Returns the limits of the tables. */
    const Limits &limits() const { return m_limits; }

    /*! Returns the number of container slots the block data gives \a block (0: no container). */
    int containerSlots(QcBlockId block) const;
    /*! Returns the machine the block data gives \a block, or null when it is none. */
    const QcMachineDefinition *machineOf(QcBlockId block) const;
    /*! Returns whether the block data marks \a block as a sapling. */
    bool isSapling(QcBlockId block) const;
    /*! Returns whether \a block is a portable container (a backpack, decision E-832). */
    bool isPortable(QcBlockId block) const;
    /*! Returns whether \a block is a portable container and the one at \a pos holds anything:
        broken, it then drops as its item carrying the contents instead of its usual drop. */
    bool carriesContents(QcBlockPos pos, QcBlockId block) const;

    /*! The block at \a pos changed from \a before to \a after (QcServerWorld::setBlock): spills
        the contents of a machine that is no machine any more and of a container that is none or
        smaller (a portable container that leaves spills as its item carrying the contents,
        decision E-832), creates the container or machine of the new block, ends a sapling's
        timer and forgets the memory that does not fit the new kind of component. Scheduled ticks
        stay: they look at whatever block is there when they run. */
    void placed(QcBlockPos pos, QcBlockId before, QcBlockId after);
    /*! The \a chunk entered the world: reconcile() and the missing containers and machines of
        the blocks in its marked \a sections (bit n: section n). */
    void chunkArrived(const QcChunk &chunk, quint32 sections);
    /*! Fits what is kept for \a chunk to its blocks (a mod took a block away, a save from another
        version): as placed() would for each entry. */
    void reconcile(const QcChunk &chunk);
    /*! Moves everything of the cell \a from to \a to (a piston pushed its block, decision
        E-337), entry by entry unless \a to has one of that kind already. */
    void relocate(QcBlockPos from, QcBlockPos to);
    /*! Forgets everything of \a chunk without spilling (it was generated again). */
    void forgetChunk(QcChunkPos chunk);
    /*! Returns the contents spilled since the last call, oldest first. */
    QList<Spill> takeSpills();
    /*! Adds \a spills, read with a chunk, to what takeSpills() returns. */
    void addSpills(const QList<Spill> &spills) { m_spills.append(spills); }

    /*! Returns the container at \a pos, or null when there is none. */
    Container *container(QcBlockPos pos);
    /*! Returns the container at \a pos, or null when there is none. */
    const Container *container(QcBlockPos pos) const;
    /*! Returns the machine at \a pos, or null when there is none. */
    Machine *machine(QcBlockPos pos);
    /*! Returns the machine at \a pos, or null when there is none. */
    const Machine *machine(QcBlockPos pos) const;
    /*! The portable container just placed at \a pos takes the contents its item \a stack carries
        (decision E-832); returns what does not fit its slots or must not lie in it, to be
        dropped. */
    [[nodiscard]] QList<QcItemStack> fillPortable(QcBlockPos pos, const QcItemStack &stack);
    /*! Returns the custom name of the container or machine at \a pos (decision E-873); empty for
        none or without one. */
    QString nameAt(QcBlockPos pos) const;
    /*! Gives the container or machine at \a pos the custom \a name (a block placed from a named
        item). Returns \c false when there is none. */
    bool setName(QcBlockPos pos, const QString &name);
    /*! Creates the container or machine of \a block at \a pos unless there is one. Returns
        \c false when the block holds none or the limit of its kind is reached. */
    bool ensure(QcBlockPos pos, QcBlockId block);
    /*! Takes \a container with its contents for setting up and loading; replaces one at the
        same position. The limits refuse only placing: what a save holds always comes back.
        Returns \c false for a container without slots. */
    bool putContainer(const Container &container);
    /*! Takes \a machine with its contents for setting up and loading, like putContainer().
        Returns \c true. */
    bool putMachine(const Machine &machine);
    /*! Calls \a visit with \c {Machine &} for every machine (QcMachineRules::tick). */
    template<typename Visit>
    void forEachMachine(Visit visit)
    {
        // NOLINTNEXTLINE(misc-const-correctness): `visit` changes the machine.
        for (Machine &machine : m_machines) {
            visit(machine);
        }
    }
    /*! Returns the number of containers. */
    qsizetype containerCount() const { return m_containers.size(); }
    /*! Returns the number of machines. */
    qsizetype machineCount() const { return m_machines.size(); }
    /*! Returns whether the container table has reached its limit. */
    bool isContainerTableFull() const { return m_containers.size() >= m_limits.containers; }
    /*! Returns whether the machine table has reached its limit. */
    bool isMachineTableFull() const { return m_machines.size() >= m_limits.machines; }

    /*! Starts the timer of the sapling block just placed at \a pos with \a remaining seconds.
        Returns \c false (with a warning) when too many grow. */
    bool plantSapling(QcBlockPos pos, double remaining);
    /*! Returns the timer of the sapling at \a pos, or null when there is none. */
    const Sapling *sapling(QcBlockPos pos) const;
    /*! Sets the seconds \a remaining of the sapling at \a pos. */
    void setSaplingRemaining(QcBlockPos pos, double remaining);
    /*! Ends the timer of the sapling at \a pos. */
    void removeSapling(QcBlockPos pos);
    /*! Returns all growing saplings in the order they were planted. */
    QList<Sapling> saplingsInOrder() const;
    /*! Returns the orders of all growing saplings, in planting order. */
    QList<quint64> saplingOrders() const;
    /*! Returns the sapling with \a order (null once it is gone; valid until it is removed). */
    Sapling *saplingByOrder(quint64 order);
    /*! Returns the number of growing saplings. */
    qsizetype saplingCount() const { return static_cast<qsizetype>(m_saplings.size()); }
    /*! Takes \a sapling from a save with its order (also beyond the limit). Returns \c false
        when its position or order is taken. */
    bool putSapling(Sapling sapling);

    /*! \brief What growSaplings-like ticks tell tickSaplings() about one sapling. */
    enum class SaplingStep : quint8 {
        /*! The timer goes on; the walk goes to the next sapling. */
        Keep,
        /*! The timer ends. */
        Remove,
        /*! The timer ends and the walk goes on after this sapling, because growing changed the
            world (and may have ended other timers). */
        Grew
    };
    /*! Calls \a visit with \c {Sapling &} for every sapling in planting order and acts on the
        SaplingStep it returns. */
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

    /*! Returns the memory of the component at \a pos, or null when it has none. */
    const Memory *memory(QcBlockPos pos) const;
    /*! Returns the memory of the component at \a pos, created when it has none. */
    Memory &memoryAt(QcBlockPos pos);
    /*! Drops memories that ran out at tick \a now: torches that have not gone out within
        \a torchWindow ticks, ended burnouts and extensions that lasted \a extensionTicks; empty
        ones go. Ended hopper cooldowns are cleared. */
    void prune(quint64 now, quint64 torchWindow, quint64 extensionTicks);

    /*! Returns the scheduled ticks of circuit components, one per position. */
    QcSignalSchedule &schedule() { return m_schedule; }
    /*! Returns the scheduled ticks of circuit components, one per position. */
    const QcSignalSchedule &schedule() const { return m_schedule; }

    /*! \brief The positions of a chunk's entries by kind: the entries live in one table per
        kind, so lookups and ticks are one hash access, and everything of a chunk is found
        without a scan. \qcfields */
    struct ChunkIndex
    {
        QList<QcBlockPos> containers;
        QList<QcBlockPos> machines;
        QList<QcBlockPos> saplings;
        QList<QcBlockPos> memories;

        /*! Returns whether the chunk has no entry. */
        bool isEmpty() const { return containers.isEmpty() && machines.isEmpty() && saplings.isEmpty() && memories.isEmpty(); }
    };

    /*! Returns the chunks with entries (scheduled ticks are kept by the schedule). */
    QList<QcChunkPos> chunks() const { return m_byChunk.keys(); }
    /*! Returns the positions of the entries of the chunk at \a pos (null for none; valid until
        the next change). */
    const ChunkIndex *chunkIndex(QcChunkPos pos) const;
    /*! Returns a copy of what the chunk at \a pos holds (tests and checks), or nothing when it
        holds nothing. */
    [[nodiscard]] std::optional<Chunk> chunk(QcChunkPos pos) const;
    /*! The chunk at \a pos left the world (QcServerWorld::unloadChunk): containers that hold
        nothing and rest and machines that are idle go; they come back with their blocks when the
        chunk arrives. */
    void chunkLeft(QcChunkPos pos);
    /*! Forgets every entry and every scheduled tick. */
    void clear();

private:
    // Files `pos` under its chunk in the list `list` of the index, or takes it out again.
    void index(QcBlockPos pos, QList<QcBlockPos> ChunkIndex::*list);
    void unindex(QcBlockPos pos, QList<QcBlockPos> ChunkIndex::*list);
    // Moves the entry of `table` at `from` to `to`, with its place in the chunk index; it stays where
    // it is when `to` has one. Whether it moved.
    template<typename Entry>
    bool relocateEntry(QHash<QcBlockPos, Entry> QcBlockEntities::*table, QList<QcBlockPos> ChunkIndex::*positions, QcBlockPos from,
                       QcBlockPos to);
    void removeContainer(QcBlockPos pos);
    void removeMachine(QcBlockPos pos);
    void removeMemory(QcBlockPos pos);
    // The missing containers and machines of the blocks of one section of a chunk.
    void ensureSection(const QcChunk &chunk, int sectionIndex);
    void placedContainer(QcBlockPos pos, QcBlockId before, QcBlockId after);
    // Spills the contents of the portable container `block` that left `pos` as its item carrying
    // them; nothing when it held nothing.
    void spillPortable(QcBlockId block, const Container &held);
    void placedMachine(QcBlockPos pos, QcBlockId after);
    // Gives the machine at `pos` the slots of `definition`; what has no slot any more spills.
    void fitMachine(QcBlockPos pos, const QcMachineDefinition &definition);
    void placedMemory(QcBlockPos pos, QcBlockId before, QcBlockId after);
    QcSignalKind kindOf(QcBlockId block) const;
    // A block state that holds no entry of any kind (no container, machine or sapling).
    bool isPlain(QcBlockId block) const { return static_cast<std::size_t>(block) < m_plain.size() && m_plain[static_cast<std::size_t>(block)]; }
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
