#pragma once

#include "QcBlockDefinition.h"
#include "QcItemDefinition.h"

// Mining rules shared by server (validation) and client (progress display).
namespace QcMining {

// Seconds per hardness point with the right tool at speed 1 (and by hand for tool-free blocks).
inline constexpr double secondsPerHardness = 1.5;
// Seconds per hardness point when the block needs a tool the player does not use.
inline constexpr double secondsPerHardnessWithoutTool = 5.0;
// Blocks with hardness 0 still take one tick, so a click cannot break a whole row at once.
inline constexpr double minimumSeconds = 0.05;

bool isBreakable(const QcBlockDefinition &block);
// True when the held item (nullptr = hand) gets the block's drops.
bool canHarvest(const QcBlockDefinition &block, const QcItemDefinition *held);
// Seconds needed to break the block; negative for unbreakable blocks.
double breakSeconds(const QcBlockDefinition &block, const QcItemDefinition *held);

} // namespace QcMining
