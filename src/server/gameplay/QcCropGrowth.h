#pragma once

#include "QcBlockAccess.h"
#include "QcContentRegistry.h"

#include <QRandomGenerator>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QcServerWorld;

// Random-tick rules of farming (decisions E-289 to E-291), called by QcPlantGrowth for blocks with
// `crop` or `soil`. A crop advances one stage with a chance that gives its `averageSeconds` from the
// first to the ripe stage on wet soil (times `dryFactor` elsewhere) and only in enough light; a ripe
// crop with a fruit (stems) puts that fruit onto a free neighbouring cell. Soil turns wet near water,
// dries step by step without it and reverts (to dirt) when dry and bare or covered by a full block.
class QcCropGrowth
{
public:
    // Sets a block and tells the clients; returns false when it could not be set.
    using SetBlock = std::function<bool(QcBlockPos pos, QcBlockId block)>;

    // `randomTickSeconds`: average seconds between two random ticks of one block.
    QcCropGrowth(std::shared_ptr<const QcContentRegistry> content, double randomTickSeconds);

    // One random tick of a crop or soil block; returns true when it changed the world.
    bool randomTick(const QcServerWorld &world, QcBlockPos pos, QcBlockId block, QRandomGenerator &random,
                    const SetBlock &setBlock) const;

    // Chance that a random tick advances `crop` by one stage (`wet`: on wet soil).
    double stageChance(QcBlockId crop, bool wet) const;
    // Chance that a random tick of a ripe stem puts its fruit down (`wet`: on wet soil).
    double fruitChance(QcBlockId crop, bool wet) const;
    // Whether the block below `pos` is soil in its wettest state.
    bool isOnWetSoil(const QcBlockAccess &world, QcBlockPos pos) const;
    enum class Water { Near, None, Unknown };
    // Whether a block named `soil.water` lies within `soil.reach` blocks of `pos` horizontally, at the
    // same level or one above (searched from the centre outwards). Unknown when none was found but
    // part of the area is not loaded: soil at a chunk border then waits instead of drying.
    Water waterNear(const QcBlockAccess &world, QcBlockPos pos, const QcSoilDefinition &soil) const;
    // Light at `pos` for plants: the brighter of block light and sky light of the world's light
    // engine (decision E-389; sky light at full strength whatever the time of day).
    static int plantLightAt(const QcServerWorld &world, QcBlockPos pos);

    // The state of soil block `soil` freshly made at `pos` (a tilled block): wet when water is near.
    QcBlockId freshSoil(const QcBlockAccess &world, QcBlockPos pos, QcBlockId soil) const;

private:
    bool tickCrop(const QcServerWorld &world, QcBlockPos pos, QcBlockId block, QRandomGenerator &random,
                  const SetBlock &setBlock) const;
    bool tickSoil(const QcServerWorld &world, QcBlockPos pos, QcBlockId block, const SetBlock &setBlock) const;
    // Puts the fruit of a ripe stem at `pos` onto a random free neighbour; false when there is none.
    bool putFruit(const QcBlockAccess &world, QcBlockPos pos, const QcCropDefinition &crop, QRandomGenerator &random,
                  const SetBlock &setBlock) const;
    // Index of the value of `property` in state `block` among the property's values; -1 if missing.
    int stageOf(QcBlockId block, const QString &property) const;
    int stageCount(QcBlockId block, const QString &property) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    double m_randomTickSeconds = 1.0;
    // Horizontal offsets within each reach (0..maxReach), nearest first.
    std::array<std::vector<std::pair<int, int>>, 9> m_ringOffsets;
};
