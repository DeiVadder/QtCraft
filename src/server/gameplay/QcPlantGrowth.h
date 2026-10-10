#pragma once

#include "QcBlockAccess.h"
#include "QcContentRegistry.h"
#include "QcPhysics.h"

#include <QRandomGenerator>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>

class QcChunk;
class QcChunkSection;
class QcServerWorld;

// Plants that grow upwards by themselves (blocks with `growth`, e.g. sugar cane), driven by random
// ticks: every server tick picks `randomTicksPerSection` random blocks in each loaded section that
// may hold such a plant (QcServerWorld::growingSections). A ticked top block of a plant column grows one block of its kind with the
// chance that gives its `averageSeconds` per step, while the column is below `maxHeight`, the block
// above is air and the column's base still has its ground (cane: water next to it).
class QcPlantGrowth
{
public:
    static constexpr int randomTicksPerSection = 3;
    // Average seconds between two random ticks of one block.
    static constexpr double randomTickSeconds = 4096.0 / randomTicksPerSection * QcPhysics::tickDuration;

    // Sets a block and tells the clients; returns false when it could not be set.
    using SetBlock = std::function<bool(QcBlockPos pos, QcBlockId block)>;

    QcPlantGrowth(std::shared_ptr<const QcContentRegistry> content, quint64 seed);

    void tick(const QcServerWorld &world, const SetBlock &setBlock);
    // One random tick of the block at `pos`; returns true when the plant grew.
    bool randomTick(const QcBlockAccess &world, QcBlockPos pos, const SetBlock &setBlock);
    // The air block a plant at `pos` would grow into, if its column may grow now.
    std::optional<QcBlockPos> growthTarget(const QcBlockAccess &world, QcBlockPos pos) const;
    // Chance that a random tick of `block` grows it.
    double growthChance(QcBlockId block) const;

private:
    bool mayHoldPlants(const QcChunkSection &section) const;
    // Picks the random cells of the sections of `chunk` marked in `sections` (bit n: section n)
    // that hold a growing plant and adds the cells holding one.
    void pickRandomPlants(const QcChunk &chunk, quint32 sections, QList<QcBlockPos> &ticked);

    std::shared_ptr<const QcContentRegistry> m_content;
    QRandomGenerator m_random;
    // Every state id of every growing block.
    QSet<QcBlockId> m_growing;
};
