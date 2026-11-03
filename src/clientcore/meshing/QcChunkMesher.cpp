// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcChunkMesher.h"

#include "clientcore/meshing/QcLiquidSurface.h"
#include "clientcore/meshing/QcMeshLighting.h"
#include "clientcore/meshing/QcMeshNeighbourhood.h"
#include "clientcore/meshing/QcMeshOutput.h"
#include "common/core/QcBlockPos.h"

#include <algorithm>

namespace {

using Entry = QcBlockRenderTable::Entry;
using Neighbourhood = QcMeshNeighbourhood;

// Corners of each face, counter-clockwise seen from outside. Corners 0 and 1 map to the bottom
// edge of the texture, 2 and 3 to the top edge.
constexpr std::array<std::array<std::array<int, 3>, 4>, 6> faceCorners = {{
    {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}}, // East
    {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}}, // West
    {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}}, // Up
    {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}}, // Down
    {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}}, // South
    {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}}, // North
}};

// The cell beside `cell` across `face`; cells are chunk-local block coordinates here (x and z may lie
// in the neighbour chunks).
QcBlockPos neighbourOf(QcBlockPos cell, QcBlockFace face)
{
    const QcFaceOffset normal = qcFaceOffset(face);
    return {cell.x + normal.dx, cell.y + normal.dy, cell.z + normal.dz};
}

// Ambient occlusion and smooth light at the four corners of a full face whose open side is the
// cell `open`.
struct FaceCorners
{
    std::array<int, 4> ao{};
    std::array<float, 4> sky{};
    std::array<float, 4> block{};
};

// A visible block being meshed: its render entry, chunk-local cell (y in the world) and its corner
// relative to the section's origin (QcChunkMesh::origin), where its vertices are placed.
struct PlacedBlock
{
    const Entry &entry;
    QcBlockPos cell;
    QcBlockPos local;
};

// A side face whose bottom corners (0 and 1) start above the cell's bottom `bottomY` shows only
// the part of its texture above them: each moves its texture coordinate towards that of the top
// corner above it (3 and 2).
std::array<QcBlockVertex, 4> croppedRaisedSide(std::array<QcBlockVertex, 4> corners, int bottomY)
{
    for (std::size_t corner = 0; corner < 2; ++corner) {
        QcBlockVertex &bottom = corners[corner];
        const QcBlockVertex &above = corners[3 - corner];
        const float raised = bottom.y - static_cast<float>(bottomY);
        bottom.u += (above.u - bottom.u) * raised;
        bottom.v += (above.v - bottom.v) * raised;
    }
    return corners;
}

// Meshes the visible blocks of one section into a QcChunkMesh.
class SectionMesher
{
public:
    SectionMesher(const QcMeshInput &input, QcChunkMesh &mesh)
        : m_table(*input.renderTable)
        , m_around(input)
        , m_light(m_around, input)
        , m_mesh(mesh)
        , m_baseY(input.sectionIndex * QcWorld::sectionHeight)
    {
    }

    const QcBlockRenderTable &table() const { return m_table; }
    const QcMeshLighting &lighting() const { return m_light; }

    void appendBlock(const Entry &entry, QcBlockPos cell)
    {
        if (entry.layer == QcRenderLayer::Invisible) {
            return;
        }

        const PlacedBlock block{entry, cell, {cell.x, cell.y - m_baseY, cell.z}};
        if (entry.fullCube) {
            appendCube(block);
        } else {
            appendShape(block);
        }
    }

private:
    static constexpr float maxLevel = static_cast<float>(QcMeshLighting::maxLevel);

    // `open` is the cell the face looks into; translucent liquids go to the water lists, cut-out
    // faces to the cutout lists (also below the world: they are never drawn without alpha test), a
    // solid face looking below the world to the underside lists.
    QcMeshOutput outputFor(const Entry &entry, QcBlockPos open)
    {
        if (entry.material == QcSurfaceMaterial::Water) {
            return {m_mesh.waterVertices, m_mesh.waterIndices, &m_mesh.translucentRuns, QcMeshOutput::Run::Water};
        }
        if (entry.layer == QcRenderLayer::Translucent) {
            return {m_mesh.translucentVertices, m_mesh.translucentIndices, &m_mesh.translucentRuns, QcMeshOutput::Run::OtherTranslucent};
        }
        if (entry.layer == QcRenderLayer::Cutout) {
            return {m_mesh.cutoutVertices, m_mesh.cutoutIndices};
        }
        if (open.y < 0) {
            return {m_mesh.undersideVertices, m_mesh.undersideIndices};
        }
        return {m_mesh.opaqueVertices, m_mesh.opaqueIndices};
    }

    FaceCorners cornersOf(QcBlockPos open, QcBlockFace face) const
    {
        FaceCorners result;
        const auto faceIndex = static_cast<std::size_t>(face);
        const auto [axisA, axisB] = QcBlockRenderTable::axes(face);
        const auto [nx, ny, nz] = open;
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const auto &c = faceCorners[faceIndex][corner];
            // Offsets towards this corner along the two axes spanning the face.
            std::array<int, 3> alongA{};
            std::array<int, 3> alongB{};
            alongA[static_cast<std::size_t>(axisA)] = c[static_cast<std::size_t>(axisA)] * 2 - 1;
            alongB[static_cast<std::size_t>(axisB)] = c[static_cast<std::size_t>(axisB)] * 2 - 1;
            const bool side1 = m_around.isOpaque(nx + alongA[0], ny + alongA[1], nz + alongA[2]);
            const bool side2 = m_around.isOpaque(nx + alongB[0], ny + alongB[1], nz + alongB[2]);
            const bool diagonal =
                m_around.isOpaque(nx + alongA[0] + alongB[0], ny + alongA[1] + alongB[1], nz + alongA[2] + alongB[2]);
            result.ao[corner] = (side1 && side2) ? 0 : 3 - static_cast<int>(side1) - static_cast<int>(side2) - static_cast<int>(diagonal);

            // Smooth lighting: average the open cells around the vertex in front of the face.
            int skySum = m_light.sky(nx, ny, nz);
            int blockSum = m_light.block(nx, ny, nz);
            int count = 1;
            const auto add = [&](int x, int y, int z) {
                skySum += m_light.sky(x, y, z);
                blockSum += m_light.block(x, y, z);
                ++count;
            };
            if (!side1) {
                add(nx + alongA[0], ny + alongA[1], nz + alongA[2]);
            }
            if (!side2) {
                add(nx + alongB[0], ny + alongB[1], nz + alongB[2]);
            }
            if (!diagonal && !(side1 && side2)) {
                add(nx + alongA[0] + alongB[0], ny + alongA[1] + alongB[1], nz + alongA[2] + alongB[2]);
            }
            result.sky[corner] = static_cast<float>(skySum) / static_cast<float>(count * QcMeshLighting::maxLevel);
            result.block[corner] = static_cast<float>(blockSum) / static_cast<float>(count * QcMeshLighting::maxLevel);
        }
        return result;
    }

    // Crosses and quads of a block that is not a full cube.
    void appendShape(const PlacedBlock &block)
    {
        const auto [x, y, z] = block.cell;
        const float ownSky = static_cast<float>(m_light.sky(x, y, z));
        const float ownBlock = static_cast<float>(m_light.block(x, y, z));
        for (const QcBlockRenderTable::Cross &cross : block.entry.crosses) {
            outputFor(block.entry, block.cell).appendCross(cross, block.local, {0.9f, ownSky / maxLevel, ownBlock / maxLevel},
                                                           block.entry.material);
        }
        for (const QcBlockRenderTable::Quad &quad : block.entry.quads) {
            appendShapeQuad(block, quad, ownSky, ownBlock);
        }
    }

    void appendShapeQuad(const PlacedBlock &block, const QcBlockRenderTable::Quad &quad, float ownSky, float ownBlock)
    {
        const QcBlockPos open = neighbourOf(block.cell, quad.face);
        const float shade = QcChunkMesher::faceShade[static_cast<std::size_t>(quad.face)];
        std::array<QcBlockVertex, 4> corners{};
        if (quad.boundary) {
            const std::optional<QcBlockId> neighbour = m_around.block(open.x, open.y, open.z);
            if (!neighbour || QcBlockRenderTable::hides(block.entry, quad.rect, quad.face, m_table.entry(*neighbour))) {
                return;
            }
            corners = shadedPartOfFace(quad, open, shade);
        } else {
            // Inside the cell: lit like the open space in front of the face.
            const float sky = std::max(ownSky, static_cast<float>(m_light.sky(open.x, open.y, open.z))) / maxLevel;
            const float light = std::max(ownBlock, static_cast<float>(m_light.block(open.x, open.y, open.z))) / maxLevel;
            for (QcBlockVertex &vertex : corners) {
                vertex.setShade(shade);
                vertex.setSkyLight(sky);
                vertex.setBlockLight(light);
            }
        }

        for (std::size_t corner = 0; corner < 4; ++corner) {
            QcBlockVertex &vertex = corners[corner];
            vertex.x = static_cast<float>(block.local.x) + quad.corners[corner][0];
            vertex.y = static_cast<float>(block.local.y) + quad.corners[corner][1];
            vertex.z = static_cast<float>(block.local.z) + quad.corners[corner][2];
            vertex.u = quad.uvs[corner][0];
            vertex.v = quad.uvs[corner][1];
        }
        outputFor(block.entry, quad.boundary ? open : block.cell).appendQuad(corners, false, {quad.face, block.entry.material});
    }

    // Corners of the part `quad` with shading and light of the full face interpolated at them (their
    // positions and texture coordinates are left at zero).
    [[nodiscard]] std::array<QcBlockVertex, 4> shadedPartOfFace(const QcBlockRenderTable::Quad &quad, QcBlockPos open, float shade) const
    {
        std::array<QcBlockVertex, 4> corners{};
        const FaceCorners full = cornersOf(open, quad.face);
        const auto [axisA, axisB] = QcBlockRenderTable::axes(quad.face);
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const float ta = quad.corners[corner][static_cast<std::size_t>(axisA)];
            const float tb = quad.corners[corner][static_cast<std::size_t>(axisB)];
            float occlusion = 0.0f;
            float sky = 0.0f;
            float block = 0.0f;
            for (std::size_t k = 0; k < 4; ++k) {
                const auto &c = faceCorners[static_cast<std::size_t>(quad.face)][k];
                const float weight = (c[static_cast<std::size_t>(axisA)] != 0 ? ta : 1.0f - ta) * (c[static_cast<std::size_t>(axisB)] != 0 ? tb : 1.0f - tb);
                occlusion += weight * QcChunkMesher::ambientOcclusionLevels[static_cast<std::size_t>(full.ao[k])];
                sky += weight * full.sky[k];
                block += weight * full.block[k];
            }

            corners[corner].setShade(shade * occlusion);
            corners[corner].setSkyLight(sky);
            corners[corner].setBlockLight(block);
        }
        return corners;
    }

    // A liquid's surface sinks with its level unless liquid lies on top of it.
    float surfaceOf(const Entry &liquid, QcBlockPos cell) const
    {
        const std::optional<QcBlockId> top = m_around.block(cell.x, cell.y + 1, cell.z);
        return top && m_table.entry(*top).liquid ? 1.0f : liquid.liquidHeight;
    }

    // The heights of the top corners of a cube (QcLiquidSurface, index x + 2 * z): 1 for a block, a
    // liquid's surface sloping towards its lower and open neighbours (decision E-885).
    std::array<float, 4> topOf(const Entry &entry, QcBlockPos at) const
    {
        const float surface = entry.liquid ? surfaceOf(entry, at) : 1.0f;
        if (surface >= 1.0f) {
            return {1.0f, 1.0f, 1.0f, 1.0f};
        }

        std::array<float, 9> cells{};
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const QcBlockPos cell{at.x + dx, at.y, at.z + dz};
                const std::optional<QcBlockId> id = m_around.block(cell.x, cell.y, cell.z);
                const Entry *next = id ? &m_table.entry(*id) : nullptr;
                float value = QcLiquidSurface::closed;
                if (next && next->liquid && next->type == entry.type) {
                    value = surfaceOf(*next, cell);
                } else if (next && next->liquidMayEnter) {
                    value = QcLiquidSurface::open;
                }
                cells[static_cast<std::size_t>(dz + 1) * 3 + static_cast<std::size_t>(dx + 1)] = value;
            }
        }
        return QcLiquidSurface::cornerHeights(cells);
    }

    void appendCube(const PlacedBlock &block)
    {
        const std::array<float, 4> top = topOf(block.entry, block.cell);
        for (const QcBlockFace face : qcAllBlockFaces) {
            appendCubeFace(block, face, top);
        }
    }

    // Heights at which a cube face towards the neighbouring cell holding `next` starts (index x + 2
    // * z), or nullopt when the face is hidden. Beside a liquid the surfaces of the same liquid meet at
    // the corners they share (QcLiquidSurface), and another liquid hides the side. A translucent
    // block's side towards translucent water starts at the water's surface (decision E-959).
    std::optional<std::array<float, 4>> cubeFaceBase(const PlacedBlock &block, const Entry &next, QcBlockFace face, std::array<float, 4> top) const
    {
        const Entry &entry = block.entry;
        const bool side = face != QcBlockFace::Up && face != QcBlockFace::Down;
        if (entry.liquid && next.liquid && side) {
            return std::nullopt;
        }

        if (entry.liquid && face == QcBlockFace::Up && std::ranges::min(top) < 1.0f && !next.liquid) {
            // A sunken surface stays visible under a ceiling.
            return std::array<float, 4>{};
        }

        if (QcBlockRenderTable::hides(entry, {}, face, next)) {
            return std::nullopt;
        }
        if (side && QcBlockRenderTable::isShore(entry, next)) {
            return QcLiquidSurface::shoreBase(face, topOf(next, neighbourOf(block.cell, face)));
        }
        return std::array<float, 4>{};
    }

    void appendCubeFace(const PlacedBlock &block, QcBlockFace face, std::array<float, 4> top)
    {
        const QcBlockPos open = neighbourOf(block.cell, face);
        const std::optional<QcBlockId> neighbour = m_around.block(open.x, open.y, open.z);
        if (!neighbour) {
            return;
        }
        const std::optional<std::array<float, 4>> base = cubeFaceBase(block, m_table.entry(*neighbour), face, top);
        if (!base) {
            return;
        }

        const auto faceIndex = static_cast<std::size_t>(face);
        const QRectF &uv = block.entry.faceUv[faceIndex];
        const std::size_t uvTurns = block.entry.faceUvTurns[faceIndex];
        const FaceCorners values = cornersOf(open, face);
        std::array<QcBlockVertex, 4> corners{};
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const auto &c = faceCorners[faceIndex][corner];
            // A turned texture gives each corner the texture corner `uvTurns` further round.
            const std::size_t uvCorner = (corner + uvTurns) % 4;
            const bool bottomEdge = uvCorner < 2;
            const bool leftEdge = uvCorner == 0 || uvCorner == 3;
            QcBlockVertex &vertex = corners[corner];
            vertex.x = static_cast<float>(block.local.x + c[0]);
            const std::size_t column = static_cast<std::size_t>(c[0]) + 2 * static_cast<std::size_t>(c[2]);
            vertex.y = static_cast<float>(block.local.y) + (c[1] == 1 ? top[column] : (*base)[column]);
            vertex.z = static_cast<float>(block.local.z + c[2]);
            vertex.u = static_cast<float>(leftEdge ? uv.left() : uv.right());
            vertex.v = static_cast<float>(bottomEdge ? uv.bottom() : uv.top());
            vertex.setShade(QcChunkMesher::faceShade[faceIndex] * QcChunkMesher::ambientOcclusionLevels[static_cast<std::size_t>(values.ao[corner])]);
            vertex.setSkyLight(values.sky[corner]);
            vertex.setBlockLight(values.block[corner]);
        }
        if (std::ranges::max(*base) > 0.0f) {
            // Only a shore side starts above the bottom (cubeFaceBase).
            corners = croppedRaisedSide(corners, block.local.y);
        }

        // Split along the brighter diagonal so occlusion gradients stay symmetric.
        outputFor(block.entry, open).appendQuad(corners, values.ao[0] + values.ao[2] < values.ao[1] + values.ao[3],
                                                {face, block.entry.material});
    }

    const QcBlockRenderTable &m_table;
    const Neighbourhood m_around;
    const QcMeshLighting m_light;
    QcChunkMesh &m_mesh;
    // The world height of the section's bottom.
    const int m_baseY = 0;
};

} // namespace

QcChunkMesh QcChunkMesher::mesh(const QcMeshInput &input)
{
    QcChunkMesh mesh;
    const auto &centre = input.chunks[4];
    if (!centre || !input.renderTable) {
        return mesh;
    }
    mesh.origin = {centre->pos().x * QcWorld::chunkSize, input.sectionIndex * QcWorld::sectionHeight, centre->pos().z * QcWorld::chunkSize};
    const QcChunkSection &section = centre->section(input.sectionIndex);
    if (section.isEmpty()) {
        return mesh;
    }

    SectionMesher mesher(input, mesh);
    const QcBlockRenderTable &table = mesher.table();
    const int baseY = input.sectionIndex * QcWorld::sectionHeight;
    for (int ly = 0; ly < QcWorld::sectionHeight; ++ly) {
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                mesher.appendBlock(table.entry(section.block(x, ly, z)), {x, baseY + ly, z});
            }
        }
    }
    if (input.sectionLight && !mesh.isEmpty()) {
        mesh.light = mesher.lighting().sectionLight(input.sectionIndex);
    }
    // All water before all other translucent faces (or only one of them) is the plain order.
    if (mesh.translucentRuns.size() <= 2) {
        mesh.translucentRuns.clear();
    }
    return mesh;
}
