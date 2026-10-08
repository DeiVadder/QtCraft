#pragma once

#include "QcBlockRenderTable.h"
#include "QcChunk.h"
#include "QcChunkLight.h"
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
    // Block light of the same nine chunks from QcBlockLightEngine (null entries are dark). Without
    // it (`hasBlockLight` false) the mesher floods the sources around the section itself.
    std::array<std::shared_ptr<const QcChunkLight>, 9> blockLight;
    bool hasBlockLight = false;
};

// Builds the triangle mesh of one chunk section from the boxes of every block state: hidden faces
// are culled, every vertex gets face shading, ambient occlusion, sky light and block light. Pure function of its input, safe to run on worker threads.
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
