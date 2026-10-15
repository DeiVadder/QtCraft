#pragma once

#include "QcServerLimits.h"
#include "QcBlockAccess.h"
#include "QcContentRegistry.h"
#include "QcCropGrowth.h"
#include "QcPhysics.h"

#include <QRandomGenerator>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>

class QcChunk;
class QcChunkSection;
class QcServerWorld;

// Blocks with random ticks: plants that grow upwards by themselves (blocks with `growth`, e.g. sugar
// cane), crops and soil (QcCropGrowth). Every server tick picks QcServerLimits::randomTicksPerSection random blocks
// in each loaded section that may hold such a block (QcServerWorld::marks, QcBlockTrait::RandomTicks). A ticked top
// block of a plant column grows one block of its kind with the chance that gives its
// `averageSeconds` per step, while the column is below `maxHeight`, the block above is air and the
// column's base still has its ground (cane: water next to it). Random ticks run only in loaded
// chunks; time a chunk spent unloaded is not made up (decision E-289).
class QcPlantGrowth
{
public:
    // Random ticks per marked section and tick by default (QcServerLimits::randomTicksPerSection).
    static constexpr int randomTicksPerSection = int(QcServerLimits{}.randomTicksPerSection);
    // Average seconds between two random ticks of one block at the default; the growth chances
    // count with it, so more random ticks make plants grow faster.
    static constexpr double randomTickSeconds = 4096.0 / randomTicksPerSection * QcPhysics::tickDuration;

    // Sets a block and tells the clients; returns false when it could not be set.
    using SetBlock = std::function<bool(QcBlockPos pos, QcBlockId block)>;

    QcPlantGrowth(std::shared_ptr<const QcContentRegistry> content, quint64 seed);

    void tick(const QcServerWorld &world, const SetBlock &setBlock);
    void setRandomTicksPerSection(int ticks) { m_randomTicksPerSection = ticks; }
    // One random tick of the block at `pos`; returns true when it changed the world.
    bool randomTick(const QcServerWorld &world, QcBlockPos pos, const SetBlock &setBlock);
    // The air block a plant at `pos` would grow into, if its column may grow now.
    [[nodiscard]] std::optional<QcBlockPos> growthTarget(const QcBlockAccess &world, QcBlockPos pos) const;
    // Chance that a random tick of `block` grows it.
    double growthChance(QcBlockId block) const;
    const QcCropGrowth &crops() const { return m_crops; }

private:
    bool mayHoldPlants(const QcChunkSection &section) const;
    // Picks the random cells of the sections of `chunk` marked in `sections` (bit n: section n)
    // that hold a growing plant and adds the cells holding one.
    void pickRandomPlants(const QcChunk &chunk, quint32 sections, QList<QcBlockPos> &ticked);

    std::shared_ptr<const QcContentRegistry> m_content;
    QRandomGenerator m_random;
    QcCropGrowth m_crops;
    int m_randomTicksPerSection = randomTicksPerSection;
    // Every state id of every block with random ticks.
    QSet<QcBlockId> m_growing;
};
