#include "server/plants/QcCropGrowth.h"

#include "server/core/QcServerWorld.h"

#include <algorithm>
#include <array>
#include <cstdlib>

QcCropGrowth::QcCropGrowth(std::shared_ptr<const QcContentRegistry> content, double randomTickSeconds)
    : m_content(std::move(content))
    , m_randomTickSeconds(randomTickSeconds)
{
    for (std::size_t reach = 0; reach < m_ringOffsets.size(); ++reach) {
        std::vector<std::pair<int, int>> &offsets = m_ringOffsets[reach];
        const int r = int(reach);
        for (int dx = -r; dx <= r; ++dx) {
            for (int dz = -r; dz <= r; ++dz) {
                offsets.emplace_back(dx, dz);
            }
        }
        std::ranges::stable_sort(offsets, {}, [](std::pair<int, int> o) { return std::max(std::abs(o.first), std::abs(o.second)); });
    }
}

bool QcCropGrowth::randomTick(const QcServerWorld &world, QcBlockPos pos, QcBlockId block, QRandomGenerator &random,
                              const SetBlock &setBlock) const
{
    if (!m_content->isValidBlock(block)) {
        return false;
    }

    const QcBlockDefinition &definition = m_content->block(block);
    if (definition.crop.isCrop()) {
        return tickCrop(world, pos, block, random, setBlock);
    }
    return definition.soil.isSoil() && tickSoil(world, pos, block, setBlock);
}

int QcCropGrowth::stageOf(QcBlockId block, const QString &property) const
{
    const QcBlockProperty *values = m_content->block(block).property(property);
    return values ? int(values->values.indexOf(m_content->property(block, property))) : -1;
}

int QcCropGrowth::stageCount(QcBlockId block, const QString &property) const
{
    const QcBlockProperty *values = m_content->block(block).property(property);
    return values ? int(values->values.size()) : 0;
}

double QcCropGrowth::stageChance(QcBlockId crop, bool wet) const
{
    const QcCropDefinition &definition = m_content->block(crop).crop;
    const int steps = stageCount(crop, definition.property) - 1;
    if (steps <= 0) {
        return 0.0;
    }

    const double perStage = definition.averageSeconds / steps;
    return std::min(1.0, m_randomTickSeconds / perStage * (wet ? 1.0 : definition.dryFactor));
}

double QcCropGrowth::fruitChance(QcBlockId crop, bool wet) const
{
    const QcCropDefinition &definition = m_content->block(crop).crop;
    if (!definition.bearsFruit()) {
        return 0.0;
    }
    return std::min(1.0, m_randomTickSeconds / definition.fruitSeconds * (wet ? 1.0 : definition.dryFactor));
}

bool QcCropGrowth::isOnWetSoil(const QcBlockAccess &world, QcBlockPos pos) const
{
    const std::optional<QcBlockId> ground = world.blockAt(pos.offset(0, -1, 0));
    if (!ground || !m_content->isValidBlock(*ground)) {
        return false;
    }

    const QcSoilDefinition &soil = m_content->block(*ground).soil;
    return soil.isSoil() && stageOf(*ground, soil.property) == stageCount(*ground, soil.property) - 1;
}

bool QcCropGrowth::tickCrop(const QcServerWorld &world, QcBlockPos pos, QcBlockId block, QRandomGenerator &random,
                            const SetBlock &setBlock) const
{
    const QcCropDefinition &crop = m_content->block(block).crop;
    const int stage = stageOf(block, crop.property);
    const int last = stageCount(block, crop.property) - 1;
    const bool ripe = stage >= last;
    if (stage < 0 || (ripe && !crop.bearsFruit())) {
        return false;
    }

    // The dice first: the light costs a walk up the column.
    const bool wet = isOnWetSoil(world, pos);
    const double chance = ripe ? fruitChance(block, wet) : stageChance(block, wet);
    if (random.generateDouble() >= chance || plantLightAt(world, pos) < crop.minLight) {
        return false;
    }

    if (ripe) {
        return putFruit(world, pos, crop, random, setBlock);
    }
    const QcBlockProperty *values = m_content->block(block).property(crop.property);
    return setBlock(pos, m_content->withProperty(block, crop.property, values->values[stage + 1]));
}

bool QcCropGrowth::putFruit(const QcBlockAccess &world, QcBlockPos pos, const QcCropDefinition &crop, QRandomGenerator &random,
                            const SetBlock &setBlock) const
{
    const std::optional<QcBlockId> fruit = m_content->blockId(crop.fruit);
    if (!fruit) {
        return false;
    }

    static constexpr std::array<std::pair<int, int>, 4> sides{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
    // One fruit per stem at a time, as long as it lies next to it.
    for (const auto &[dx, dz] : sides) {
        const std::optional<QcBlockId> neighbour = world.blockAt(pos.offset(dx, 0, dz));
        if (neighbour && m_content->isValidBlock(*neighbour) && m_content->isSameBlock(*neighbour, *fruit)) {
            return false;
        }
    }

    const auto &[dx, dz] = sides[random.bounded(quint32(sides.size()))];
    const QcBlockPos target = pos.offset(dx, 0, dz);
    const std::optional<QcBlockId> existing = world.blockAt(target);
    const std::optional<QcBlockId> ground = world.blockAt(target.offset(0, -1, 0));
    const bool free = existing && (*existing == qcAirBlockId
                                   || (m_content->block(*existing).replaceable && !m_content->block(*existing).liquid));
    if (!free || !ground || !m_content->isValidBlock(*ground) || !crop.fruitGround.contains(m_content->block(*ground).name)) {
        return false;
    }
    return setBlock(target, *fruit);
}

QcCropGrowth::Water QcCropGrowth::waterNear(const QcBlockAccess &world, QcBlockPos pos, const QcSoilDefinition &soil) const
{
    bool unknown = false;
    for (const auto &[dx, dz] : m_ringOffsets[std::size_t(std::clamp(soil.reach, 0, int(m_ringOffsets.size()) - 1))]) {
        for (int dy = 0; dy <= 1; ++dy) {
            const std::optional<QcBlockId> block = world.blockAt(pos.offset(dx, dy, dz));
            if (!block) {
                unknown = true;
            } else if (m_content->isValidBlock(*block) && m_content->block(*block).name == soil.water) {
                return Water::Near;
            }
        }
    }
    return unknown ? Water::Unknown : Water::None;
}

bool QcCropGrowth::tickSoil(const QcServerWorld &world, QcBlockPos pos, QcBlockId block, const SetBlock &setBlock) const
{
    const QcSoilDefinition &soil = m_content->block(block).soil;
    const std::optional<QcBlockId> above = world.blockAt(pos.offset(0, 1, 0));
    const std::optional<QcBlockId> reverted = m_content->blockId(soil.revertsTo);
    if (!above || !m_content->isValidBlock(*above) || !reverted) {
        return false;
    }

    // A full block on top presses the soil back into its plain form.
    if (m_content->state(*above).opaque) {
        return setBlock(pos, *reverted);
    }

    const QcBlockProperty *values = m_content->block(block).property(soil.property);
    const int stage = stageOf(block, soil.property);
    if (!values || stage < 0) {
        return false;
    }

    const int wettest = int(values->values.size()) - 1;
    // Rain on the cell above wets it like water near it.
    const bool rained = m_rainsOn && m_rainsOn(pos.offset(0, 1, 0));
    const Water water = rained ? Water::Near : waterNear(world, pos, soil);
    if (water == Water::Unknown) {
        return false;
    }
    if (water == Water::Near) {
        return stage != wettest && setBlock(pos, m_content->withProperty(block, soil.property, values->values[wettest]));
    }
    if (stage > 0) {
        return setBlock(pos, m_content->withProperty(block, soil.property, values->values[stage - 1]));
    }
    // Dry soil keeps as long as a crop stands on it.
    return !m_content->block(*above).crop.isCrop() && setBlock(pos, *reverted);
}

QcBlockId QcCropGrowth::freshSoil(const QcBlockAccess &world, QcBlockPos pos, QcBlockId soil) const
{
    const QcSoilDefinition &definition = m_content->block(soil).soil;
    const QcBlockProperty *values = m_content->block(soil).property(definition.property);
    if (!definition.isSoil() || !values || values->values.isEmpty()) {
        return soil;
    }

    // Without a decision (water may lie in a chunk that is not loaded) it starts dry; its next
    // random tick with the area loaded wets it.
    const QString &value = waterNear(world, pos, definition) == Water::Near ? values->values.constLast() : values->values.constFirst();
    return m_content->withProperty(soil, definition.property, value);
}

int QcCropGrowth::plantLightAt(const QcServerWorld &world, QcBlockPos pos)
{
    return std::max(world.blockLight(pos), world.skyLight(pos));
}
