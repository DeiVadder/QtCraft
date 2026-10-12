#pragma once

namespace QcWorld {

// One block is one cubic metre; all lengths below are in blocks (= metres).
inline constexpr int chunkSize = 16;
inline constexpr int sectionHeight = 16;
inline constexpr int sectionCount = 16;
inline constexpr int worldHeight = sectionHeight * sectionCount;
inline constexpr int seaLevel = 62;
inline constexpr int blocksPerSection = chunkSize * chunkSize * sectionHeight;
// Horizontal extent of the world in blocks from the origin; keeps all coordinate arithmetic in range.
inline constexpr int maxHorizontalBlock = 30'000'000;
inline constexpr int maxHorizontalChunk = maxHorizontalBlock / chunkSize;
// The void below the world bottom (decision E-288): nothing blocks a fall there, and from this
// height on players take void damage while everything else is removed.
inline constexpr double voidLimitY = -64.0;
// Bodies stop sinking here, far below the void limit: players die on the way (at most about
// 120 m below the limit) and everything else is removed, so it only holds a player in a world
// whose time stands still (/tick freeze) and keeps saved heights bounded.
inline constexpr double voidFloorY = -256.0;
constexpr bool isInVoid(double y)
{
    return y < voidLimitY;
}

} // namespace QcWorld
