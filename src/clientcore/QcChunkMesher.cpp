#include "QcChunkMesher.h"

#include "QcLightVolume.h"

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

// Sky and block light of the 3 x 3 chunk neighbourhood around one section, with a margin of one
// chunk horizontally and 16 blocks vertically, which is as far as light travels (levels 15 to 0).
// Block light comes from the input's light snapshots or, without them, from every source in that
// volume, so it is complete for the section either way.
class Lighting
{
public:
    static constexpr int extent = 3 * QcWorld::chunkSize;
    static constexpr int margin = QcChunkMesher::lightMargin;
    static constexpr int maxLevel = 15;

    Lighting(Neighbourhood around, const QcMeshInput &input)
        : m_minY(std::max(0, input.sectionIndex * QcWorld::sectionHeight - margin))
        , m_maxY(std::min(QcWorld::worldHeight - 1, (input.sectionIndex + 1) * QcWorld::sectionHeight - 1 + margin))
        , m_height(m_maxY - m_minY + 1)
        , m_levels(qsizetype(extent) * extent * m_height, 0)
        , m_opacity(qsizetype(extent) * extent * m_height, 0)
        , m_blockLight(input.renderTable->hasLightSources() && !input.hasBlockLight ? extent : 0, m_height, extent)
        , m_input(input)
    {
        const QcBlockRenderTable &table = *input.renderTable;
        const bool blockLight = table.hasLightSources() && !input.hasBlockLight;
        QList<int> skyTop(qsizetype(extent) * extent, 0);
        for (int z = 0; z < extent; ++z) {
            for (int x = 0; x < extent; ++x) {
                const int lx = x - QcWorld::chunkSize;
                const int lz = z - QcWorld::chunkSize;
                // Highest block that dims sky light; everything above it is fully lit.
                int top = -1;
                const int chunkIndex = (lz < 0 ? 0 : (lz >= QcWorld::chunkSize ? 2 : 1)) * 3
                                       + (lx < 0 ? 0 : (lx >= QcWorld::chunkSize ? 2 : 1));
                const auto &chunk = input.chunks[std::size_t(chunkIndex)];
                if (!chunk) {
                    top = QcWorld::worldHeight;
                } else {
                    for (int section = QcWorld::sectionCount - 1; section >= 0 && top < 0; --section) {
                        if (chunk->section(section).isEmpty()) {
                            continue;
                        }
                        for (int y = (section + 1) * QcWorld::sectionHeight - 1; y >= section * QcWorld::sectionHeight; --y) {
                            const std::optional<QcBlockId> id = around.block(lx, y, lz);
                            if (id && table.entry(*id).lightOpacity > 0) {
                                top = y;
                                break;
                            }
                        }
                    }
                }
                skyTop[z * extent + x] = top;
                for (int y = m_minY; y <= m_maxY; ++y) {
                    const std::optional<QcBlockId> id = around.block(lx, y, lz);
                    const Entry &entry = table.entry(id.value_or(qcAirBlockId));
                    const int opacity = id ? entry.lightOpacity : maxLevel;
                    m_opacity[index(x, y, z)] = quint8(opacity);
                    if (y > top) {
                        m_levels[index(x, y, z)] = quint8(maxLevel);
                    }
                    if (blockLight) {
                        m_blockLight.setOpacity(x, y - m_minY, z, opacity);
                        if (id && entry.lightLevel > 0) {
                            m_blockLight.addSource(x, y - m_minY, z, entry.lightLevel);
                        }
                    }
                }
            }
        }
        propagate(skyTop);
        m_blockLight.propagate();
    }

    // Light levels of a block given in chunk-local coordinates; outside the volume counts as dark.
    int sky(int lx, int y, int lz) const
    {
        const int x = lx + QcWorld::chunkSize;
        const int z = lz + QcWorld::chunkSize;
        if (x < 0 || x >= extent || z < 0 || z >= extent || y < m_minY || y > m_maxY) {
            return y >= QcWorld::worldHeight ? maxLevel : 0;
        }
        return m_levels[index(x, y, z)];
    }
    int block(int lx, int y, int lz) const
    {
        if (!m_input.hasBlockLight) {
            return m_blockLight.level(lx + QcWorld::chunkSize, y - m_minY, lz + QcWorld::chunkSize);
        }
        const int cx = lx < 0 ? -1 : (lx >= QcWorld::chunkSize ? 1 : 0);
        const int cz = lz < 0 ? -1 : (lz >= QcWorld::chunkSize ? 1 : 0);
        const auto &light = m_input.blockLight[std::size_t(cz + 1) * 3 + std::size_t(cx + 1)];
        return light ? light->level(lx - cx * QcWorld::chunkSize, y, lz - cz * QcWorld::chunkSize) : 0;
    }

private:
    qsizetype index(int x, int y, int z) const { return (qsizetype(y - m_minY) * extent + z) * extent + x; }

    void propagate(const QList<int> &skyTop)
    {
        // Breadth-first flood from lit cells into darker ones; each step costs at least one level.
        QList<std::array<int, 3>> queue;
        for (int z = 0; z < extent; ++z) {
            for (int x = 0; x < extent; ++x) {
                const int top = skyTop[z * extent + x];
                // Only the lit cells next to the shadowed part of the volume can spread light further.
                for (int y = std::max(m_minY, top + 1); y <= std::min(m_maxY, top + 2); ++y) {
                    queue.append({x, y, z});
                }
                for (const auto &[dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                    const int nx = x + dx;
                    const int nz = z + dz;
                    if (nx < 0 || nx >= extent || nz < 0 || nz >= extent) {
                        continue;
                    }
                    const int neighbourTop = skyTop[nz * extent + nx];
                    for (int y = std::max(m_minY, top + 3); y <= std::min(m_maxY, neighbourTop); ++y) {
                        queue.append({x, y, z});
                    }
                }
            }
        }
        for (qsizetype head = 0; head < queue.size(); ++head) {
            const auto [x, y, z] = queue[head];
            const int light = m_levels[index(x, y, z)];
            if (light <= 1) {
                continue;
            }
            for (const QcBlockFace face : qcAllBlockFaces) {
                const QcFaceOffset offset = qcFaceOffset(face);
                const int nx = x + offset.dx;
                const int ny = y + offset.dy;
                const int nz = z + offset.dz;
                if (nx < 0 || nx >= extent || nz < 0 || nz >= extent || ny < m_minY || ny > m_maxY) {
                    continue;
                }
                const qsizetype neighbour = index(nx, ny, nz);
                const int opacity = m_opacity[neighbour];
                const int candidate = light - std::max(1, opacity);
                const int current = m_levels[neighbour];
                if (opacity < maxLevel && candidate > current) {
                    m_levels[neighbour] = quint8(candidate);
                    queue.append({nx, ny, nz});
                }
            }
        }
    }

    int m_minY = 0;
    int m_maxY = 0;
    int m_height = 0;
    QList<quint8> m_levels;
    QList<quint8> m_opacity;
    QcLightVolume m_blockLight;
    const QcMeshInput &m_input;
};

// Ambient occlusion and smooth light at the four corners of a full face whose open side is the
// cell (nx, ny, nz).
struct FaceCorners
{
    std::array<int, 4> ao{};
    std::array<float, 4> sky{};
    std::array<float, 4> block{};
};

FaceCorners cornersOf(Neighbourhood around, const Lighting &light, int nx, int ny, int nz, QcBlockFace face)
{
    FaceCorners result;
    const auto faceIndex = std::size_t(face);
    const auto [axisA, axisB] = QcBlockRenderTable::axes(face);
    for (std::size_t corner = 0; corner < 4; ++corner) {
        const auto &c = faceCorners[faceIndex][corner];
        // Offsets towards this corner along the two axes spanning the face.
        std::array<int, 3> alongA{};
        std::array<int, 3> alongB{};
        alongA[std::size_t(axisA)] = c[std::size_t(axisA)] * 2 - 1;
        alongB[std::size_t(axisB)] = c[std::size_t(axisB)] * 2 - 1;
        const bool side1 = around.isOpaque(nx + alongA[0], ny + alongA[1], nz + alongA[2]);
        const bool side2 = around.isOpaque(nx + alongB[0], ny + alongB[1], nz + alongB[2]);
        const bool diagonal =
            around.isOpaque(nx + alongA[0] + alongB[0], ny + alongA[1] + alongB[1], nz + alongA[2] + alongB[2]);
        result.ao[corner] = (side1 && side2) ? 0 : 3 - int(side1) - int(side2) - int(diagonal);
        // Smooth lighting: average the open cells around the vertex in front of the face.
        int skySum = light.sky(nx, ny, nz);
        int blockSum = light.block(nx, ny, nz);
        int count = 1;
        const auto add = [&](int x, int y, int z) {
            skySum += light.sky(x, y, z);
            blockSum += light.block(x, y, z);
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
        result.sky[corner] = float(skySum) / float(count * Lighting::maxLevel);
        result.block[corner] = float(blockSum) / float(count * Lighting::maxLevel);
    }
    return result;
}

void appendQuad(QList<QcBlockVertex> &vertices, QList<quint32> &indices, const std::array<QcBlockVertex, 4> &corners,
                bool flipDiagonal)
{
    const auto base = quint32(vertices.size());
    for (const QcBlockVertex &vertex : corners) {
        vertices.append(vertex);
    }
    if (flipDiagonal) {
        indices.append({base + 1, base + 2, base + 3, base + 1, base + 3, base});
    } else {
        indices.append({base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

// Two quads along the diagonals of `cross`, each with a front and a back side so back-face culling
// keeps both visible.
void appendCross(QcChunkMesh &mesh, const Entry &entry, const QcBlockRenderTable::Cross &cross, QcBlockPos pos, float sky,
                 float block)
{
    const bool translucent = entry.layer == QcRenderLayer::Translucent;
    QList<QcBlockVertex> &vertices = translucent ? mesh.translucentVertices : mesh.opaqueVertices;
    QList<quint32> &indices = translucent ? mesh.translucentIndices : mesh.opaqueIndices;
    const QRectF &uv = cross.uv;
    const auto &[x0, y0, z0, x1, y1, z1] = cross.box;
    const std::array<std::array<float, 4>, 2> diagonals = {{{x0, z0, x1, z1}, {x0, z1, x1, z0}}};
    for (const auto &[ax, az, bx, bz] : diagonals) {
        for (const bool back : {false, true}) {
            const std::array<std::array<float, 2>, 2> ends = back ? std::array<std::array<float, 2>, 2>{{{bx, bz}, {ax, az}}}
                                                                   : std::array<std::array<float, 2>, 2>{{{ax, az}, {bx, bz}}};
            std::array<QcBlockVertex, 4> corners{};
            for (std::size_t corner = 0; corner < 4; ++corner) {
                const auto &end = ends[(corner == 1 || corner == 2) ? 1 : 0];
                QcBlockVertex &vertex = corners[corner];
                vertex.x = float(pos.x) + end[0];
                vertex.y = float(pos.y) + (corner < 2 ? y0 : y1);
                vertex.z = float(pos.z) + end[1];
                vertex.u = float((corner == 0 || corner == 3) ? uv.left() : uv.right());
                vertex.v = float(corner < 2 ? uv.bottom() : uv.top());
                vertex.shade = 0.9f;
                vertex.skyLight = sky;
                vertex.blockLight = block;
            }
            appendQuad(vertices, indices, corners, false);
        }
    }
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
    const Lighting light(around, input);
    const int baseY = input.sectionIndex * QcWorld::sectionHeight;
    const int worldX = centre->pos().x * QcWorld::chunkSize;
    const int worldZ = centre->pos().z * QcWorld::chunkSize;
    constexpr float maxLevel = float(Lighting::maxLevel);

    for (int ly = 0; ly < QcWorld::sectionHeight; ++ly) {
        const int y = baseY + ly;
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                const QcBlockId id = section.block(x, ly, z);
                const Entry &entry = table.entry(id);
                if (entry.layer == QcRenderLayer::Invisible) {
                    continue;
                }
                const bool translucent = entry.layer == QcRenderLayer::Translucent;
                QList<QcBlockVertex> &vertices = translucent ? mesh.translucentVertices : mesh.opaqueVertices;
                QList<quint32> &indices = translucent ? mesh.translucentIndices : mesh.opaqueIndices;
                const QcBlockPos world{worldX + x, y, worldZ + z};

                if (!entry.fullCube) {
                    const float ownSky = float(light.sky(x, y, z));
                    const float ownBlock = float(light.block(x, y, z));
                    for (const QcBlockRenderTable::Cross &cross : entry.crosses) {
                        appendCross(mesh, entry, cross, world, ownSky / maxLevel, ownBlock / maxLevel);
                    }
                    for (const QcBlockRenderTable::Quad &quad : entry.quads) {
                        const QcFaceOffset normal = qcFaceOffset(quad.face);
                        const int nx = x + normal.dx;
                        const int ny = y + normal.dy;
                        const int nz = z + normal.dz;
                        const float shade = faceShade[std::size_t(quad.face)];
                        std::array<QcBlockVertex, 4> corners{};
                        if (quad.boundary) {
                            const std::optional<QcBlockId> neighbour = around.block(nx, ny, nz);
                            if (!neighbour || QcBlockRenderTable::hides(entry, quad.rect, quad.face, table.entry(*neighbour))) {
                                continue;
                            }
                            // Values of the full face, interpolated at the corners of the part.
                            const FaceCorners full = cornersOf(around, light, nx, ny, nz, quad.face);
                            const auto [axisA, axisB] = QcBlockRenderTable::axes(quad.face);
                            for (std::size_t corner = 0; corner < 4; ++corner) {
                                const float ta = quad.corners[corner][std::size_t(axisA)];
                                const float tb = quad.corners[corner][std::size_t(axisB)];
                                float occlusion = 0.0f;
                                float sky = 0.0f;
                                float block = 0.0f;
                                for (std::size_t k = 0; k < 4; ++k) {
                                    const auto &c = faceCorners[std::size_t(quad.face)][k];
                                    const float weight = (c[std::size_t(axisA)] != 0 ? ta : 1.0f - ta)
                                                         * (c[std::size_t(axisB)] != 0 ? tb : 1.0f - tb);
                                    occlusion += weight * ambientOcclusionLevels[std::size_t(full.ao[k])];
                                    sky += weight * full.sky[k];
                                    block += weight * full.block[k];
                                }
                                corners[corner].shade = shade * occlusion;
                                corners[corner].skyLight = sky;
                                corners[corner].blockLight = block;
                            }
                        } else {
                            // Inside the cell: lit like the open space in front of the face.
                            const float sky = std::max(ownSky, float(light.sky(nx, ny, nz))) / maxLevel;
                            const float block = std::max(ownBlock, float(light.block(nx, ny, nz))) / maxLevel;
                            for (QcBlockVertex &vertex : corners) {
                                vertex.shade = shade;
                                vertex.skyLight = sky;
                                vertex.blockLight = block;
                            }
                        }
                        for (std::size_t corner = 0; corner < 4; ++corner) {
                            QcBlockVertex &vertex = corners[corner];
                            vertex.x = float(world.x) + quad.corners[corner][0];
                            vertex.y = float(world.y) + quad.corners[corner][1];
                            vertex.z = float(world.z) + quad.corners[corner][2];
                            vertex.u = quad.uvs[corner][0];
                            vertex.v = quad.uvs[corner][1];
                        }
                        appendQuad(vertices, indices, corners, false);
                    }
                    continue;
                }

                // A liquid's surface sinks with its level unless liquid lies on top of it.
                const auto surfaceOf = [&](const Entry &liquid, int lx, int ly2, int lz) {
                    const std::optional<QcBlockId> top = around.block(lx, ly2 + 1, lz);
                    return top && table.entry(*top).liquid ? 1.0f : liquid.liquidHeight;
                };
                const float surface = entry.liquid ? surfaceOf(entry, x, y, z) : 1.0f;
                for (const QcBlockFace face : qcAllBlockFaces) {
                    const QcFaceOffset normal = qcFaceOffset(face);
                    const int nx = x + normal.dx;
                    const int ny = y + normal.dy;
                    const int nz = z + normal.dz;
                    const std::optional<QcBlockId> neighbour = around.block(nx, ny, nz);
                    if (!neighbour) {
                        continue;
                    }
                    const Entry &next = table.entry(*neighbour);
                    const bool side = face != QcBlockFace::Up && face != QcBlockFace::Down;
                    // Bottom of a side face: above a lower liquid of the same kind only the step shows.
                    float base = 0.0f;
                    if (entry.liquid && next.liquid && side) {
                        const float nextSurface = surfaceOf(next, nx, ny, nz);
                        if (next.type != entry.type || nextSurface >= surface) {
                            continue;
                        }
                        base = nextSurface;
                    } else if (entry.liquid && face == QcBlockFace::Up && surface < 1.0f && !next.liquid) {
                        // A sunken surface stays visible under a ceiling.
                    } else if (QcBlockRenderTable::hides(entry, {}, face, next)) {
                        continue;
                    }
                    const auto faceIndex = std::size_t(face);
                    const QRectF &uv = entry.faceUv[faceIndex];
                    const FaceCorners values = cornersOf(around, light, nx, ny, nz, face);
                    std::array<QcBlockVertex, 4> corners{};
                    for (std::size_t corner = 0; corner < 4; ++corner) {
                        const auto &c = faceCorners[faceIndex][corner];
                        const float vy = float(y) + (c[1] == 1 ? surface : base);
                        const bool bottomEdge = corner < 2;
                        const bool leftEdge = corner == 0 || corner == 3;
                        QcBlockVertex &vertex = corners[corner];
                        vertex.x = float(world.x + c[0]);
                        vertex.y = vy;
                        vertex.z = float(world.z + c[2]);
                        vertex.u = float(leftEdge ? uv.left() : uv.right());
                        vertex.v = float(bottomEdge ? uv.bottom() : uv.top());
                        vertex.shade = faceShade[faceIndex] * ambientOcclusionLevels[std::size_t(values.ao[corner])];
                        vertex.skyLight = values.sky[corner];
                        vertex.blockLight = values.block[corner];
                    }
                    // Split along the brighter diagonal so occlusion gradients stay symmetric.
                    appendQuad(vertices, indices, corners, values.ao[0] + values.ao[2] < values.ao[1] + values.ao[3]);
                }
            }
        }
    }
    return mesh;
}
