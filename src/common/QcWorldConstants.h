#pragma once

namespace QcWorld {

// One block is one cubic metre; all lengths below are in blocks (= metres).
inline constexpr int chunkSize = 16;
inline constexpr int sectionHeight = 16;
inline constexpr int sectionCount = 16;
inline constexpr int worldHeight = sectionHeight * sectionCount;
inline constexpr int seaLevel = 62;
inline constexpr int blocksPerSection = chunkSize * chunkSize * sectionHeight;

} // namespace QcWorld
