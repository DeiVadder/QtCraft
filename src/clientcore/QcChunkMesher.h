#pragma once

#include "QcBlockRenderTable.h"
#include "QcChunk.h"
#include "QcChunkMesh.h"

#include <array>
#include <memory>

// Immutable snapshot of everything needed to mesh one section: the chunk and its eight neighbours
// (for faces and ambient occlusion across chunk borders). Missing neighbours count as opaque.
struct QcMeshInput
{
    // Index (dz + 1) * 3 + (dx + 1); index 4 is the chunk being meshed.
    std::array<std::shared_ptr<const QcChunk>, 9> chunks;
    int sectionIndex = 0;
    std::shared_ptr<const QcBlockRenderTable> renderTable;
};

// Builds the triangle mesh of one chunk section: hidden faces are culled, every vertex gets face
// shading and ambient occlusion. Pure function of its input, safe to run on worker threads.
class QcChunkMesher
{
public:
    // Shading of faces by direction, indexed by QcBlockFace.
    static constexpr std::array<float, 6> faceShade = {0.65f, 0.65f, 1.0f, 0.5f, 0.8f, 0.8f};
    // Brightness by number of occluders around a vertex (0 = fully occluded corner).
    static constexpr std::array<float, 4> ambientOcclusionLevels = {0.45f, 0.65f, 0.82f, 1.0f};
    // Depth by which the surface of a liquid lies below the block top.
    static constexpr float liquidSurfaceDrop = 0.125f;

    static QcChunkMesh mesh(const QcMeshInput &input);
};
