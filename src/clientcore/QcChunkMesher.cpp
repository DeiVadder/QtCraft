#include "QcChunkMesher.h"

namespace {

using Entry = QcBlockRenderTable::Entry;

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

class Neighbourhood
{
public:
    explicit Neighbourhood(const QcMeshInput &input)
        : m_input(input)
    {
    }

    // Block at chunk-local coordinates; x and z may lie in [-16, 32).
    // Returns nullopt where the neighbour chunk is missing or y is below the world.
    std::optional<QcBlockId> block(int x, int y, int z) const
    {
        if (y < 0) {
            return std::nullopt;
        }
        if (y >= QcWorld::worldHeight) {
            return qcAirBlockId;
        }
        const int cx = x < 0 ? -1 : (x >= QcWorld::chunkSize ? 1 : 0);
        const int cz = z < 0 ? -1 : (z >= QcWorld::chunkSize ? 1 : 0);
        const int index = (cz + 1) * 3 + (cx + 1);
        const auto &chunk = m_input.chunks[std::size_t(index)];
        if (!chunk) {
            return std::nullopt;
        }
        return chunk->block(x - cx * QcWorld::chunkSize, y, z - cz * QcWorld::chunkSize);
    }

    bool isOpaque(int x, int y, int z) const
    {
        const std::optional<QcBlockId> id = block(x, y, z);
        return !id || m_input.renderTable->entry(*id).opaque;
    }

private:
    const QcMeshInput &m_input;
};

bool faceVisible(QcBlockId self, const Entry &selfEntry, std::optional<QcBlockId> neighbour, const QcBlockRenderTable &table)
{
    if (!neighbour) {
        return false;
    }
    const Entry &other = table.entry(*neighbour);
    if (other.opaque) {
        return false;
    }
    if (*neighbour == self && selfEntry.layer == QcRenderLayer::Translucent) {
        return false;
    }
    return !(selfEntry.liquid && other.liquid);
}

} // namespace

QcChunkMesh QcChunkMesher::mesh(const QcMeshInput &input)
{
    QcChunkMesh mesh;
    const auto &centre = input.chunks[4];
    if (!centre || !input.renderTable) {
        return mesh;
    }
    const QcChunkSection &section = centre->section(input.sectionIndex);
    if (section.isEmpty()) {
        return mesh;
    }
    const QcBlockRenderTable &table = *input.renderTable;
    const Neighbourhood around(input);
    const int baseY = input.sectionIndex * QcWorld::sectionHeight;
    const int worldX = centre->pos().x * QcWorld::chunkSize;
    const int worldZ = centre->pos().z * QcWorld::chunkSize;

    for (int ly = 0; ly < QcWorld::sectionHeight; ++ly) {
        const int y = baseY + ly;
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                const QcBlockId id = section.block(x, ly, z);
                const Entry &entry = table.entry(id);
                if (entry.layer == QcRenderLayer::Invisible) {
                    continue;
                }
                const std::optional<QcBlockId> above = around.block(x, y + 1, z);
                const bool lowerTop = entry.liquid && !(above && table.entry(*above).liquid);
                const bool translucent = entry.layer == QcRenderLayer::Translucent;
                QList<QcBlockVertex> &vertices = translucent ? mesh.translucentVertices : mesh.opaqueVertices;
                QList<quint32> &indices = translucent ? mesh.translucentIndices : mesh.opaqueIndices;

                for (const QcBlockFace face : qcAllBlockFaces) {
                    const QcFaceOffset normal = qcFaceOffset(face);
                    const int nx = x + normal.dx;
                    const int ny = y + normal.dy;
                    const int nz = z + normal.dz;
                    if (!faceVisible(id, entry, around.block(nx, ny, nz), table)) {
                        continue;
                    }
                    const auto faceIndex = std::size_t(face);
                    const int normalAxis = normal.dx != 0 ? 0 : (normal.dy != 0 ? 1 : 2);
                    const int axisA = normalAxis == 0 ? 1 : 0;
                    const int axisB = normalAxis == 2 ? 1 : 2;
                    const QRectF &uv = entry.faceUv[faceIndex];
                    std::array<int, 4> ao{};
                    const auto base = quint32(vertices.size());
                    for (std::size_t corner = 0; corner < 4; ++corner) {
                        const auto &c = faceCorners[faceIndex][corner];
                        // Offsets towards this corner along the two axes spanning the face.
                        std::array<int, 3> alongA{};
                        std::array<int, 3> alongB{};
                        alongA[std::size_t(axisA)] = c[std::size_t(axisA)] * 2 - 1;
                        alongB[std::size_t(axisB)] = c[std::size_t(axisB)] * 2 - 1;
                        const bool side1 = around.isOpaque(nx + alongA[0], ny + alongA[1], nz + alongA[2]);
                        const bool side2 = around.isOpaque(nx + alongB[0], ny + alongB[1], nz + alongB[2]);
                        const bool diagonal = around.isOpaque(nx + alongA[0] + alongB[0], ny + alongA[1] + alongB[1],
                                                              nz + alongA[2] + alongB[2]);
                        ao[corner] = (side1 && side2) ? 0 : 3 - int(side1) - int(side2) - int(diagonal);

                        float vy = float(y + c[1]);
                        if (lowerTop && c[1] == 1) {
                            vy -= liquidSurfaceDrop;
                        }
                        const bool bottomEdge = corner < 2;
                        const bool leftEdge = corner == 0 || corner == 3;
                        QcBlockVertex vertex;
                        vertex.x = float(worldX + x + c[0]);
                        vertex.y = vy;
                        vertex.z = float(worldZ + z + c[2]);
                        vertex.u = float(leftEdge ? uv.left() : uv.right());
                        vertex.v = float(bottomEdge ? uv.bottom() : uv.top());
                        vertex.shade = faceShade[faceIndex] * ambientOcclusionLevels[std::size_t(ao[corner])];
                        vertex.skyLight = 1.0f;
                        vertices.append(vertex);
                    }
                    // Split along the brighter diagonal so occlusion gradients stay symmetric.
                    if (ao[0] + ao[2] >= ao[1] + ao[3]) {
                        indices.append({base, base + 1, base + 2, base, base + 2, base + 3});
                    } else {
                        indices.append({base + 1, base + 2, base + 3, base + 1, base + 3, base});
                    }
                }
            }
        }
    }
    return mesh;
}
