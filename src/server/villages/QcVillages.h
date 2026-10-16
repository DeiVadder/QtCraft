#pragma once

#include "QcChunk.h"
#include "QcVillage.h"
#include "QcVillageBlocks.h"

#include <QHash>
#include <QList>

#include <memory>

class QcWorldGenerator;
struct QcVillagePlan;

// The villages of a world (decision E-403), loaded or not: kept in villages.json. A village becomes
// known when the first chunk of it enters the world, with the beds and workstations of the
// generator's plan; every chunk of it that enters later is scanned and its beds and workstations
// replace those listed there, and every block set in the world updates the list (players place and
// break beds). Lives in QcServerWorld, on the server thread.
class QcVillages
{
public:
    struct Limits
    {
        qsizetype villages = 4096;
        qsizetype beds = 64;
        qsizetype workstations = 64;
    };

    QcVillages(const QcContentRegistry &content, const QcWorldGenerator &generator);

    void setLimits(const Limits &limits) { m_limits = limits; }
    const Limits &limits() const { return m_limits; }
    const QcVillageBlocks &blocks() const { return m_blocks; }

    // A chunk entered the world.
    void noteChunk(const QcChunk &chunk);
    // A block was set in a loaded chunk.
    void noteBlock(QcBlockPos pos, QcBlockId before, QcBlockId after);

    qsizetype count() const { return m_villages.size(); }
    const QcVillage *find(qint64 id) const;
    QcVillage *find(qint64 id);
    // The village whose area holds `pos`, if any.
    const QcVillage *at(QcBlockPos pos) const;
    // The village whose centre lies nearest to `pos` (horizontally), within `reach` blocks.
    const QcVillage *nearest(QcBlockPos pos, double reach) const;
    // Every village, ordered by id (saving, tests).
    QList<QcVillage> villages() const;
    // The villages of a saved world (villages.json); later entries beyond the limit are left out.
    void restore(const QList<QcVillage> &villages);
    // The village of a generator plan, its spots cut to the limits.
    QcVillage fromPlan(const QcVillagePlan &plan) const;
    // Counts every change (saving writes villages.json only after one); villagers claiming spots
    // or leaving (QcVillagerHomes) count theirs.
    quint64 changes() const { return m_changes; }
    void noteChanged() { ++m_changes; }
    // Villagers living in all villages (QcVillage::population).
    qsizetype population() const;

private:
    void add(const QcVillage &village);
    // Replaces the village's spots inside the chunk with those the chunk holds.
    void rescan(QcVillage &village, const QcChunk &chunk);
    // Adds the beds and workstations the chunk holds inside the village.
    void scan(QcVillage &village, const QcChunk &chunk);
    void addSpot(QcVillage &village, QcBlockPos pos, QcBlockId block);
    QcVillage *villageAt(QcBlockPos pos);

    const QcWorldGenerator &m_generator;
    QcVillageBlocks m_blocks;
    Limits m_limits;
    QHash<qint64, QcVillage> m_villages;
    // The villages whose area reaches into a chunk.
    QHash<QcChunkPos, QList<qint64>> m_byChunk;
    quint64 m_changes = 0;
};
