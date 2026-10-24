#pragma once

namespace QcWorld {

// One block is one cubic metre; all lengths below are in blocks (= metres).
inline constexpr int chunkSize = 16;
inline constexpr int sectionHeight = 16;
inline constexpr int sectionCount = 16;
inline constexpr int worldHeight = sectionHeight * sectionCount;
inline constexpr int seaLevel = 62;
inline constexpr int blocksPerSection = chunkSize * chunkSize * sectionHeight;
// Horizontal extent of the world from the origin (decision E-883): the technical bound, not a
// border of the game. Chunks from -maxHorizontalChunk to maxHorizontalChunk exist, the blocks within
// ±maxHorizontalBlock (2^30 - 16) lie in them. Block coordinates are int and overflow at 2^31, so
// twice a coordinate and the sum of two still fit; region files (32 chunks) and the 24-bit region
// fields of village ids (regions of at least 128 blocks) stay in range; bodies (double) keep a
// resolution of about 2.4e-7 m there; the renderer draws relative to the camera (QcDrawOrigins).
// Bodies stop at the centre of the outermost block column (lowestBody, highestBody) and the server
// loads no chunk beyond, so no save can hold one.
inline constexpr int maxHorizontalChunk = (1 << 26) - 1;
inline constexpr int maxHorizontalBlock = maxHorizontalChunk * chunkSize;
// The farthest block column of the chunks within the bound: the positive edge chunk reaches
// chunkSize - 1 columns beyond maxHorizontalBlock. Saved positions up to here are valid.
inline constexpr int maxChunkBlock = maxHorizontalBlock + chunkSize - 1;
// The range a body's x and z stay in (decision E-883): from the centre of the outermost block column
// on the negative side (-maxHorizontalBlock) to the centre of the one on the positive side
// (maxChunkBlock), the same distance from the last chunk's outer face on both sides.
inline constexpr double lowestBody = -maxHorizontalBlock + 0.5;
inline constexpr double highestBody = maxChunkBlock + 0.5;
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
