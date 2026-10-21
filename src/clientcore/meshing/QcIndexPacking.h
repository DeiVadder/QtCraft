#pragma once

#include <QtGlobal>

// Index buffers of section meshes: two bytes per index whenever every vertex can be addressed
// with 16 bits, otherwise four (QcPackedMesh writes them).
namespace QcIndexPacking {

// Largest vertex count whose indices (0 .. count - 1) fit into 16 bits.
inline constexpr qsizetype maxShortIndexVertices = 65536;

constexpr bool fitsShortIndices(qsizetype vertexCount)
{
    return vertexCount <= maxShortIndexVertices;
}

} // namespace QcIndexPacking
