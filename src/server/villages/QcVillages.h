// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcXzRect.h"
#include "common/world/QcChunk.h"
#include "server/villages/QcVillage.h"
#include "worldgen/villages/QcVillageBlocks.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <functional>
#include <memory>

class QcWorldGenerator;
struct QcVillagePlan;

/*! \brief The villages of a world (decision E-403), loaded or not: kept in villages.json.

    A village becomes known when the first chunk of it enters the world, with the beds and
    workstations of the generator's plan; every chunk of it that enters later is scanned and its
    beds and workstations replace those listed there, and every block set in the world updates
    the list (players place and break beds). A bed or workstation set outside every village area
    joins the nearest village within joinDistance, whose area grows to hold it, or founds a
    settlement (decision E-490), unless the grown or new area would meet another village's: a
    spot belongs to exactly one village (at()). A bed set while a village holds \c {Limits::beds}
    beds is not listed, not even once one of its beds is broken. A settlement without spots and
    villagers is gone. While a chunk of a village's area is loaded, the village's plan is held
    here (decision E-416), so the generator's plan cache never plans it again on the server
    thread. Lives in QcServerWorld, on the server thread. */
class QcVillages
{
public:
    /*! \brief Bounds of the villages (QcServerLimits). */
    struct Limits
    {
        /*! The most villages known at once. */
        qsizetype villages = 4096;
        /*! The most beds of one village. */
        qsizetype beds = 64;
        /*! The most workstations of one village. */
        qsizetype workstations = 64;
        /*! A village grows (joinDistance) at most to a box this many blocks wide and deep. */
        int extent = 384;
    };

    /*! A bed or workstation set at most this far (horizontally) from a village's box joins it. */
    static constexpr int joinDistance = 32;

    /*! Reads the village blocks from \a content; plans come from \a generator. Logs the
        warnings of the village blocks (unknown workstation blocks) in qtcraft.content. */
    QcVillages(const QcContentRegistry &content, const QcWorldGenerator &generator);

    /*! Sets limits() to \a limits. */
    void setLimits(const Limits &limits) { m_limits = limits; }
    /*! Returns the bounds of the villages. */
    const Limits &limits() const { return m_limits; }
    /*! Returns the blocks that count for villages: beds, workstations, bells. */
    const QcVillageBlocks &blocks() const { return m_blocks; }
    /*! Sets \a loaded, which tells whether a chunk is loaded (QcServerWorld): a village that
        becomes known holds its plan for the chunks of its area that entered before it did. */
    void setLoadedQuery(std::function<bool(QcChunkPos)> loaded) { m_loaded = std::move(loaded); }

    /*! The chunk \a chunk entered the world. */
    void noteChunk(const QcChunk &chunk);
    /*! The chunk at \a pos left the world: the plans of villages without loaded chunks are let
        go. */
    void forgetChunk(QcChunkPos pos);
    /*! The block at \a pos in a loaded chunk changed from \a before to \a after. Setting a bed or
        workstation where the village lists the same already changes nothing (its claim stays). */
    void noteBlock(QcBlockPos pos, QcBlockId before, QcBlockId after);
    /*! A bed or workstation \a block at \a pos the generator placed outside its village plans
        (decision E-490, QcWorldGenerator::pointsOfInterestIn) entered the world in its chunk: it
        counts as placed, after the generated villages within reach of it became known. Idempotent:
        a chunk entering again changes nothing. */
    void notePointOfInterest(QcBlockPos pos, QcBlockId block);

    /*! Returns the number of known villages. */
    qsizetype count() const { return m_villages.size(); }
    /*! Returns the village with the id \a id (QcVillage::id), or null. */
    const QcVillage *find(qint64 id) const;
    /*! Returns the village with the id \a id for changes, or null. */
    QcVillage *find(qint64 id);
    /*! Returns the generator's plan of the village \a id: the one held while a chunk of it is
        loaded, else asked of the generator; null for an unknown village or one the generator has no
        plan of. */
    std::shared_ptr<const QcVillagePlan> planOf(qint64 id) const;
    /*! Returns the villages whose plan is held (a chunk of their area is loaded). */
    qsizetype heldPlans() const { return m_held.size(); }
    /*! Returns the villages whose area (with \c {reach}) reaches into a chunk at most \a chunks
        chunks from \a pos. */
    QList<qint64> near(QcChunkPos pos, int chunks) const;
    /*! Returns the village whose area holds \a pos, if any; where areas overlap, the one whose
        box lies nearest (the lower id on a tie): every spot belongs to exactly one village. */
    const QcVillage *at(QcBlockPos pos) const;
    /*! Returns the village whose centre lies nearest to \a pos (horizontally), within \a reach
        blocks. */
    const QcVillage *nearest(QcBlockPos pos, double reach) const;
    /*! Returns every village, ordered by id (saving, tests). */
    QList<QcVillage> villages() const;
    /*! Takes \a villages of a saved world (villages.json); later entries beyond the limit are
        left out. */
    void restore(const QList<QcVillage> &villages);
    /*! Returns the village of the generator plan \a plan, its spots cut to the limits. */
    QcVillage fromPlan(const QcVillagePlan &plan) const;
    /*! Forgets the village \a id when it is a settlement without beds, workstations and
        villagers (decision E-490). */
    void dropIfEmpty(qint64 id);
    /*! Returns the number of changes (saving writes villages.json only after one); villagers
        claiming spots or leaving (QcVillagerHomes) count theirs. */
    quint64 changes() const { return m_changes; }
    /*! Counts a change (changes()). */
    void noteChanged() { ++m_changes; }
    /*! Returns the villagers living in all villages (QcVillage::population). */
    qsizetype population() const;
    /*! Returns the golems of one kind in all villages: per village as many as \a loaded says
        (golems of the kind in the world now, by the village whose area holds them), or for a
        village whose golems are of that kind (\a ofKind) as many as it counted last
        (QcVillage::golems) where that is more. */
    qsizetype golems(const QHash<qint64, int> &loaded, const std::function<bool(const QcVillage &)> &ofKind) const;

private:
    // How far `pos` lies from the village's box (horizontally, the larger of both axes; 0 inside).
    static int boxDistance(const QcVillage &village, QcBlockPos pos);
    // Whether the box with `reach` meets the area of a village other than `except`.
    bool clashes(QcXzRect box, qint64 except) const;
    // Whether the box with `reach` meets the area of one of the villages `ids` other than `except`.
    bool meets(const QList<qint64> &ids, QcXzRect box, qint64 except) const;
    // `plan` is the village's generator plan if at hand (else asked of the generator when needed).
    void add(const QcVillage &village, const std::shared_ptr<const QcVillagePlan> &plan = nullptr);
    // Lists the village's area by the chunks it reaches into (m_byChunk), or takes it out.
    void index(const QcVillage &village);
    void unindex(const QcVillage &village);
    // The village a bed or workstation at `pos` outside every village area belongs to: the nearest
    // one within joinDistance, grown to hold it, or a new settlement; null beyond the limits or where
    // every such area would meet another village's.
    QcVillage *homeFor(QcBlockPos pos);
    // The generator's village of the chunk's region becomes known when it reaches into the chunk;
    // returns that plan (null without one).
    std::shared_ptr<const QcVillagePlan> learn(QcChunkPos pos);
    // Whether the village lists the bed or workstation `block` at `pos` already.
    bool listed(const QcVillage &village, QcBlockPos pos, QcBlockId block) const;
    // Widens the village's box to hold `pos`.
    void grow(QcVillage &village, QcBlockPos pos);
    // Holds the plan of a village that just became known for its chunks that are loaded already.
    void holdLoaded(const QcVillage &village, const std::shared_ptr<const QcVillagePlan> &plan);
    // Replaces the village's spots inside the chunk with those the chunk holds.
    void rescan(QcVillage &village, const QcChunk &chunk);
    // Adds the beds and workstations the chunk holds inside the village.
    void scan(QcVillage &village, const QcChunk &chunk);
    void addSpot(QcVillage &village, QcBlockPos pos, QcBlockId block);
    QcVillage *villageAt(QcBlockPos pos);
    // Holds the plan of the village while the chunk is loaded; `regionPlan` is the plan of the
    // chunk's own region (the village's when it lies in that region).
    void hold(const QcVillage &village, QcChunkPos pos, const std::shared_ptr<const QcVillagePlan> &regionPlan);

    struct Held
    {
        std::shared_ptr<const QcVillagePlan> plan;
        QSet<QcChunkPos> chunks;
    };

    const QcWorldGenerator &m_generator;
    QcVillageBlocks m_blocks;
    Limits m_limits;
    QHash<qint64, QcVillage> m_villages;
    // The villages whose area reaches into a chunk.
    QHash<QcChunkPos, QList<qint64>> m_byChunk;
    // The plans of the villages with loaded chunks in their area, and those chunks.
    QHash<qint64, Held> m_held;
    std::function<bool(QcChunkPos)> m_loaded;
    quint64 m_changes = 0;
};
