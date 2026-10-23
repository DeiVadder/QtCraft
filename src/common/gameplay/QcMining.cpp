#include "common/gameplay/QcMining.h"

#include <algorithm>

namespace QcMining {

bool isBreakable(const QcBlockDefinition &block)
{
    return block.hardness >= 0.0 && !block.liquid && block.renderLayer != QcRenderLayer::Invisible;
}

namespace {

bool usesMatchingTool(const QcBlockDefinition &block, const QcItemDefinition *held)
{
    return held && !block.tool.isEmpty() && held->tool == block.tool;
}

} // namespace

bool canHarvest(const QcBlockDefinition &block, const QcItemDefinition *held)
{
    if (block.toolLevel <= 0) {
        return true;
    }
    return usesMatchingTool(block, held) && held->toolLevel >= block.toolLevel;
}

double breakSeconds(const QcBlockDefinition &block, const QcItemDefinition *held, double speedBonus)
{
    if (!isBreakable(block)) {
        return -1.0;
    }
    double seconds = block.hardness * (canHarvest(block, held) ? secondsPerHardness : secondsPerHardnessWithoutTool);
    if (usesMatchingTool(block, held)) {
        seconds /= std::max(0.1, held->toolSpeed + speedBonus);
    }
    return std::max(minimumSeconds, seconds);
}

} // namespace QcMining
