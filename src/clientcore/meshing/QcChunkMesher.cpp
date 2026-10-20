#include "clientcore/meshing/QcChunkMesher.h"

#include "clientcore/light/QcSectionLight.h"
#include "common/world/QcLightPassage.h"
#include "common/world/QcLightVolume.h"

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
    // Returns nullopt where the neighbour chunk is missing. Below and above the world is air, so
    // the underside of the bottom layer is drawn for a view from the void (decision E-288).
    std::optional<QcBlockId> block(int x, int y, int z) const
    {
        if (y < 0 || y >= QcWorld::worldHeight) {
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
        , m_blockedFaces(qsizetype(extent) * extent * m_height, 0)
        , m_floodsBlockLight(input.renderTable->hasLightSources() && !input.hasBlockLight)
        , m_blockLight(m_floodsBlockLight ? extent : 0, m_height, extent)
        , m_input(input)
    {
        QList<int> skyTop(qsizetype(extent) * extent, 0);
        for (int z = 0; z < extent; ++z) {
            for (int x = 0; x < extent; ++x) {
                const int top = columnTop(around, x - QcWorld::chunkSize, z - QcWorld::chunkSize);
                skyTop[z * extent + x] = top;
                fillColumn(around, x, z, top);
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

    // The section's own cells of both channels for shaders that read light per cell (decision
    // E-390): block light shares the snapshot of the input when there is one, sky light (and block
    // light flooded here) is copied out of the volume.
    QcSectionLight sectionLight(int sectionIndex) const
    {
        const int baseY = sectionIndex * QcWorld::sectionHeight;
        std::shared_ptr<const QcLightNibbles> blockCells;
        if (m_input.hasBlockLight) {
            const auto &chunk = m_input.blockLight[4];
            blockCells = chunk ? chunk->sections[std::size_t(sectionIndex)] : nullptr;
        } else {
            blockCells = copyCells(baseY, [this](int x, int y, int z) { return block(x, y, z); });
        }

        const auto skyCells = copyCells(baseY, [this](int x, int y, int z) { return m_levels[index(x + QcWorld::chunkSize, y, z + QcWorld::chunkSize)]; });
        return QcSectionLight::fromChannels(std::move(blockCells), skyCells);
    }

private:
    qsizetype index(int x, int y, int z) const { return (qsizetype(y - m_minY) * extent + z) * extent + x; }

    // The levels of the section starting at `baseY`, read cell by cell (chunk-local coordinates).
    template<typename Level>
    static std::shared_ptr<const QcLightNibbles> copyCells(int baseY, Level level)
    {
        auto cells = std::make_shared<QcLightNibbles>();
        for (int y = 0; y < QcWorld::sectionHeight; ++y) {
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    cells->setLevel(QcChunkSection::index(x, y, z), int(level(x, baseY + y, z)));
                }
            }
        }
        return cells;
    }

    // Highest cell of the column that sky light coming straight down does not reach; everything
    // above it is fully lit (QcLightPassage::skyStop).
    int columnTop(Neighbourhood around, int lx, int lz) const
    {
        const int chunkIndex = (lz < 0 ? 0 : (lz >= QcWorld::chunkSize ? 2 : 1)) * 3
                               + (lx < 0 ? 0 : (lx >= QcWorld::chunkSize ? 2 : 1));
        const auto &chunk = m_input.chunks[std::size_t(chunkIndex)];
        if (!chunk) {
            return QcWorld::worldHeight;
        }

        const QcBlockRenderTable &table = *m_input.renderTable;
        for (int section = QcWorld::sectionCount - 1; section >= 0; --section) {
            if (chunk->section(section).isEmpty()) {
                continue;
            }
            for (int y = (section + 1) * QcWorld::sectionHeight - 1; y >= section * QcWorld::sectionHeight; --y) {
                const std::optional<QcBlockId> id = around.block(lx, y, lz);
                const QcBlockRenderTable::Entry &entry = table.entry(id.value_or(qcAirBlockId));
                const QcLightPassage::SkyStop stop = QcLightPassage::skyStop(entry.lightOpacity, entry.lightBlockedFaces);
                if (id && stop != QcLightPassage::SkyStop::None) {
                    return QcLightPassage::skyTop(stop, y);
                }
            }
        }
        return -1;
    }

    // Opacity, full sky light above `top` and block light sources of one column of the volume.
    void fillColumn(Neighbourhood around, int x, int z, int top)
    {
        const QcBlockRenderTable &table = *m_input.renderTable;
        const int lx = x - QcWorld::chunkSize;
        const int lz = z - QcWorld::chunkSize;
        for (int y = m_minY; y <= m_maxY; ++y) {
            const std::optional<QcBlockId> id = around.block(lx, y, lz);
            const Entry &entry = table.entry(id.value_or(qcAirBlockId));
            const int opacity = id ? entry.lightOpacity : maxLevel;
            m_opacity[index(x, y, z)] = quint8(opacity);
            m_blockedFaces[index(x, y, z)] = entry.lightBlockedFaces;
            if (y > top) {
                m_levels[index(x, y, z)] = quint8(maxLevel);
            }

            if (!m_floodsBlockLight) {
                continue;
            }
            m_blockLight.setOpacity(x, y - m_minY, z, opacity, entry.lightBlockedFaces);
            if (id && entry.lightLevel > 0) {
                m_blockLight.addSource(x, y - m_minY, z, entry.lightLevel);
            }
        }
    }

    // Only the lit cells next to the shadowed part of the volume can spread light further; queues
    // those of column (x, z) for the flood.
    void queueSkyEdge(const QList<int> &skyTop, int x, int z)
    {
        const int top = skyTop[z * extent + x];
        for (int y = std::max(m_minY, top + 1); y <= std::min(m_maxY, top + 2); ++y) {
            m_queue.append({x, y, z});
        }

        for (const auto &[dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const int nx = x + dx;
            const int nz = z + dz;
            if (nx < 0 || nx >= extent || nz < 0 || nz >= extent) {
                continue;
            }
            const int neighbourTop = skyTop[nz * extent + nx];
            for (int y = std::max(m_minY, top + 3); y <= std::min(m_maxY, neighbourTop); ++y) {
                m_queue.append({x, y, z});
            }
        }
    }

    void propagate(const QList<int> &skyTop)
    {
        // Breadth-first flood from lit cells into darker ones; each step costs at least one level.
        for (int z = 0; z < extent; ++z) {
            for (int x = 0; x < extent; ++x) {
                queueSkyEdge(skyTop, x, z);
            }
        }

        QList<std::array<int, 3>> &queue = m_queue;

        for (qsizetype head = 0; head < queue.size(); ++head) {
            const auto [x, y, z] = queue[head];
            const int light = m_levels[index(x, y, z)];
            if (light <= 1) {
                continue;
            }
            const quint8 blocked = m_blockedFaces[index(x, y, z)];
            for (const QcBlockFace face : qcAllBlockFaces) {
                const QcFaceOffset offset = qcFaceOffset(face);
                const int nx = x + offset.dx;
                const int ny = y + offset.dy;
                const int nz = z + offset.dz;
                if (nx < 0 || nx >= extent || nz < 0 || nz >= extent || ny < m_minY || ny > m_maxY) {
                    continue;
                }
                const qsizetype neighbour = index(nx, ny, nz);
                if (!QcLightPassage::crosses(blocked, m_blockedFaces[neighbour], face)) {
                    continue;
                }
                const int opacity = m_opacity[neighbour];
                const int candidate = light - std::max(1, opacity);
                const int current = m_levels[neighbour];
                if (opacity < maxLevel && candidate > current) {
                    m_levels[neighbour] = quint8(candidate);
                    queue.append({nx, ny, nz});
                }
            }
        }
        // The queue is only needed while flooding.
        m_queue = {};
    }

    int m_minY = 0;
    int m_maxY = 0;
    int m_height = 0;
    QList<quint8> m_levels;
    QList<quint8> m_opacity;
    QList<quint8> m_blockedFaces;
    // Cells of the sky light flood (only while the constructor floods).
    QList<std::array<int, 3>> m_queue;
    // Without light snapshots in the input the block light is flooded here.
    bool m_floodsBlockLight = false;
    QcLightVolume m_blockLight;
    const QcMeshInput &m_input;
};

// Chunk-local block coordinates (x and z may lie in the neighbour chunks).
struct Cell
{
    int x = 0;
    int y = 0;
    int z = 0;
};

Cell neighbourOf(Cell cell, QcBlockFace face)
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

// Shading, sky light and block light given to all four vertices of a quad.
struct QuadLight
{
    float shade = 1.0f;
    float sky = 0.0f;
    float block = 0.0f;
};

// Vertex and index lists of one render layer of the mesh. Water and the other translucent faces
// also note the order in which they alternate (QcChunkMesh::translucentRuns; `runKind` 0: water,
// 1: other translucent faces).
struct Output
{
    QList<QcBlockVertex> &vertices;
    QList<quint32> &indices;
    QList<quint32> *runs = nullptr;
    int runKind = 0;

    // Six more indices of the kind `runKind` in the runs: the last run grows or a new one starts
    // (the first run is water, possibly empty).
    void noteRun() const
    {
        if (runs->isEmpty() && runKind == 1) {
            runs->append(0);
        }
        if (!runs->isEmpty() && (runs->size() - 1) % 2 == runKind) {
            runs->back() += 6;
        } else {
            runs->append(6);
        }
    }
};

// Normal and material written into the surface byte of a quad's vertices (decision E-390).
struct Surface
{
    std::optional<QcBlockFace> normal;
    QcSurfaceMaterial material = QcSurfaceMaterial::Solid;
};

// A visible block being meshed: its render entry, chunk-local cell and world position.
struct PlacedBlock
{
    const Entry &entry;
    Cell cell;
    QcBlockPos world;
};

void appendQuad(const Output &out, const std::array<QcBlockVertex, 4> &corners, bool flipDiagonal, Surface surface)
{
    const auto base = quint32(out.vertices.size());
    for (QcBlockVertex vertex : corners) {
        vertex.setSurface(surface.normal, surface.material);
        out.vertices.append(vertex);
    }
    if (flipDiagonal) {
        out.indices.append({base + 1, base + 2, base + 3, base + 1, base + 3, base});
    } else {
        out.indices.append({base, base + 1, base + 2, base, base + 2, base + 3});
    }
    if (out.runs) {
        out.noteRun();
    }
}

// One side of one diagonal quad of a cross, running from ends[0] to ends[1].
std::array<QcBlockVertex, 4> crossSide(const QcBlockRenderTable::Cross &cross, std::array<std::array<float, 2>, 2> ends,
                                       QcBlockPos pos, QuadLight light)
{
    const QRectF &uv = cross.uv;
    const float y0 = cross.box[1];
    const float y1 = cross.box[4];

    std::array<QcBlockVertex, 4> corners{};
    for (std::size_t corner = 0; corner < 4; ++corner) {
        const auto &end = ends[(corner == 1 || corner == 2) ? 1 : 0];
        QcBlockVertex &vertex = corners[corner];
        vertex.x = float(pos.x) + end[0];
        vertex.y = float(pos.y) + (corner < 2 ? y0 : y1);
        vertex.z = float(pos.z) + end[1];
        vertex.u = float((corner == 0 || corner == 3) ? uv.left() : uv.right());
        vertex.v = float(corner < 2 ? uv.bottom() : uv.top());
        vertex.setShade(light.shade);
        vertex.setSkyLight(light.sky);
        vertex.setBlockLight(light.block);
    }

    return corners;
}

// Two quads along the diagonals of `cross`, each with a front and a back side so back-face culling
// keeps both visible. Without a normal; plants unless the block's material says more (light sources).
void appendCross(const Output &out, const QcBlockRenderTable::Cross &cross, QcBlockPos pos, QuadLight light, QcSurfaceMaterial material)
{
    const bool plant = material == QcSurfaceMaterial::Solid || material == QcSurfaceMaterial::Cutout;
    const Surface surface{std::nullopt, plant ? QcSurfaceMaterial::Plant : material};
    const auto &[x0, y0, z0, x1, y1, z1] = cross.box;
    const std::array<std::array<float, 4>, 2> diagonals = {{{x0, z0, x1, z1}, {x0, z1, x1, z0}}};
    for (const auto &[ax, az, bx, bz] : diagonals) {
        for (const bool back : {false, true}) {
            const std::array<std::array<float, 2>, 2> ends = back ? std::array<std::array<float, 2>, 2>{{{bx, bz}, {ax, az}}}
                                                                   : std::array<std::array<float, 2>, 2>{{{ax, az}, {bx, bz}}};
            appendQuad(out, crossSide(cross, ends, pos, light), false, surface);
        }
    }
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
        , m_worldX(input.chunks[4]->pos().x * QcWorld::chunkSize)
        , m_worldZ(input.chunks[4]->pos().z * QcWorld::chunkSize)
    {
    }

    const QcBlockRenderTable &table() const { return m_table; }
    const Lighting &lighting() const { return m_light; }

    void appendBlock(const Entry &entry, Cell cell)
    {
        if (entry.layer == QcRenderLayer::Invisible) {
            return;
        }

        const PlacedBlock block{entry, cell, {m_worldX + cell.x, cell.y, m_worldZ + cell.z}};
        if (entry.fullCube) {
            appendCube(block);
        } else {
            appendShape(block);
        }
    }

private:
    static constexpr float maxLevel = float(Lighting::maxLevel);

    // `open` is the cell the face looks into; an opaque face looking below the world goes to the
    // underside lists, translucent liquids to the water lists.
    Output outputFor(const Entry &entry, Cell open)
    {
        if (entry.material == QcSurfaceMaterial::Water) {
            return {m_mesh.waterVertices, m_mesh.waterIndices, &m_mesh.translucentRuns, 0};
        }
        if (entry.layer == QcRenderLayer::Translucent) {
            return {m_mesh.translucentVertices, m_mesh.translucentIndices, &m_mesh.translucentRuns, 1};
        }
        if (open.y < 0) {
            return {m_mesh.undersideVertices, m_mesh.undersideIndices};
        }
        return {m_mesh.opaqueVertices, m_mesh.opaqueIndices};
    }

    FaceCorners cornersOf(Cell open, QcBlockFace face) const
    {
        FaceCorners result;
        const auto faceIndex = std::size_t(face);
        const auto [axisA, axisB] = QcBlockRenderTable::axes(face);
        const auto [nx, ny, nz] = open;
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const auto &c = faceCorners[faceIndex][corner];
            // Offsets towards this corner along the two axes spanning the face.
            std::array<int, 3> alongA{};
            std::array<int, 3> alongB{};
            alongA[std::size_t(axisA)] = c[std::size_t(axisA)] * 2 - 1;
            alongB[std::size_t(axisB)] = c[std::size_t(axisB)] * 2 - 1;
            const bool side1 = m_around.isOpaque(nx + alongA[0], ny + alongA[1], nz + alongA[2]);
            const bool side2 = m_around.isOpaque(nx + alongB[0], ny + alongB[1], nz + alongB[2]);
            const bool diagonal =
                m_around.isOpaque(nx + alongA[0] + alongB[0], ny + alongA[1] + alongB[1], nz + alongA[2] + alongB[2]);
            result.ao[corner] = (side1 && side2) ? 0 : 3 - int(side1) - int(side2) - int(diagonal);

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
            result.sky[corner] = float(skySum) / float(count * Lighting::maxLevel);
            result.block[corner] = float(blockSum) / float(count * Lighting::maxLevel);
        }
        return result;
    }

    // Crosses and quads of a block that is not a full cube.
    void appendShape(const PlacedBlock &block)
    {
        const auto [x, y, z] = block.cell;
        const float ownSky = float(m_light.sky(x, y, z));
        const float ownBlock = float(m_light.block(x, y, z));
        for (const QcBlockRenderTable::Cross &cross : block.entry.crosses) {
            appendCross(outputFor(block.entry, block.cell), cross, block.world, {0.9f, ownSky / maxLevel, ownBlock / maxLevel},
                        block.entry.material);
        }
        for (const QcBlockRenderTable::Quad &quad : block.entry.quads) {
            appendShapeQuad(block, quad, ownSky, ownBlock);
        }
    }

    void appendShapeQuad(const PlacedBlock &block, const QcBlockRenderTable::Quad &quad, float ownSky, float ownBlock)
    {
        const Cell open = neighbourOf(block.cell, quad.face);
        const float shade = QcChunkMesher::faceShade[std::size_t(quad.face)];
        std::array<QcBlockVertex, 4> corners{};
        if (quad.boundary) {
            const std::optional<QcBlockId> neighbour = m_around.block(open.x, open.y, open.z);
            if (!neighbour || QcBlockRenderTable::hides(block.entry, quad.rect, quad.face, m_table.entry(*neighbour))) {
                return;
            }
            corners = shadedPartOfFace(quad, open, shade);
        } else {
            // Inside the cell: lit like the open space in front of the face.
            const float sky = std::max(ownSky, float(m_light.sky(open.x, open.y, open.z))) / maxLevel;
            const float light = std::max(ownBlock, float(m_light.block(open.x, open.y, open.z))) / maxLevel;
            for (QcBlockVertex &vertex : corners) {
                vertex.setShade(shade);
                vertex.setSkyLight(sky);
                vertex.setBlockLight(light);
            }
        }

        for (std::size_t corner = 0; corner < 4; ++corner) {
            QcBlockVertex &vertex = corners[corner];
            vertex.x = float(block.world.x) + quad.corners[corner][0];
            vertex.y = float(block.world.y) + quad.corners[corner][1];
            vertex.z = float(block.world.z) + quad.corners[corner][2];
            vertex.u = quad.uvs[corner][0];
            vertex.v = quad.uvs[corner][1];
        }
        appendQuad(outputFor(block.entry, quad.boundary ? open : block.cell), corners, false, {quad.face, block.entry.material});
    }

    // Corners of the part `quad` with shading and light of the full face interpolated at them (their
    // positions and texture coordinates are left at zero).
    [[nodiscard]] std::array<QcBlockVertex, 4> shadedPartOfFace(const QcBlockRenderTable::Quad &quad, Cell open, float shade) const
    {
        std::array<QcBlockVertex, 4> corners{};
        const FaceCorners full = cornersOf(open, quad.face);
        const auto [axisA, axisB] = QcBlockRenderTable::axes(quad.face);
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const float ta = quad.corners[corner][std::size_t(axisA)];
            const float tb = quad.corners[corner][std::size_t(axisB)];
            float occlusion = 0.0f;
            float sky = 0.0f;
            float block = 0.0f;
            for (std::size_t k = 0; k < 4; ++k) {
                const auto &c = faceCorners[std::size_t(quad.face)][k];
                const float weight = (c[std::size_t(axisA)] != 0 ? ta : 1.0f - ta) * (c[std::size_t(axisB)] != 0 ? tb : 1.0f - tb);
                occlusion += weight * QcChunkMesher::ambientOcclusionLevels[std::size_t(full.ao[k])];
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
    float surfaceOf(const Entry &liquid, Cell cell) const
    {
        const std::optional<QcBlockId> top = m_around.block(cell.x, cell.y + 1, cell.z);
        return top && m_table.entry(*top).liquid ? 1.0f : liquid.liquidHeight;
    }

    void appendCube(const PlacedBlock &block)
    {
        const float surface = block.entry.liquid ? surfaceOf(block.entry, block.cell) : 1.0f;
        for (const QcBlockFace face : qcAllBlockFaces) {
            appendCubeFace(block, face, surface);
        }
    }

    // Height at which a cube face towards `next` starts (above a lower liquid of the same kind only
    // the step shows), or nullopt when the face is hidden.
    std::optional<float> cubeFaceBase(const PlacedBlock &block, const Entry &next, Cell open, QcBlockFace face, float surface) const
    {
        const Entry &entry = block.entry;
        const bool side = face != QcBlockFace::Up && face != QcBlockFace::Down;
        if (entry.liquid && next.liquid && side) {
            const float nextSurface = surfaceOf(next, open);
            if (next.type != entry.type || nextSurface >= surface) {
                return std::nullopt;
            }
            return nextSurface;
        }

        if (entry.liquid && face == QcBlockFace::Up && surface < 1.0f && !next.liquid) {
            // A sunken surface stays visible under a ceiling.
            return 0.0f;
        }

        if (QcBlockRenderTable::hides(entry, {}, face, next)) {
            return std::nullopt;
        }
        return 0.0f;
    }

    void appendCubeFace(const PlacedBlock &block, QcBlockFace face, float surface)
    {
        const Cell open = neighbourOf(block.cell, face);
        const std::optional<QcBlockId> neighbour = m_around.block(open.x, open.y, open.z);
        if (!neighbour) {
            return;
        }
        const std::optional<float> base = cubeFaceBase(block, m_table.entry(*neighbour), open, face, surface);
        if (!base) {
            return;
        }

        const auto faceIndex = std::size_t(face);
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
            vertex.x = float(block.world.x + c[0]);
            vertex.y = float(block.cell.y) + (c[1] == 1 ? surface : *base);
            vertex.z = float(block.world.z + c[2]);
            vertex.u = float(leftEdge ? uv.left() : uv.right());
            vertex.v = float(bottomEdge ? uv.bottom() : uv.top());
            vertex.setShade(QcChunkMesher::faceShade[faceIndex] * QcChunkMesher::ambientOcclusionLevels[std::size_t(values.ao[corner])]);
            vertex.setSkyLight(values.sky[corner]);
            vertex.setBlockLight(values.block[corner]);
        }

        // Split along the brighter diagonal so occlusion gradients stay symmetric.
        appendQuad(outputFor(block.entry, open), corners, values.ao[0] + values.ao[2] < values.ao[1] + values.ao[3],
                   {face, block.entry.material});
    }

    const QcBlockRenderTable &m_table;
    const Neighbourhood m_around;
    const Lighting m_light;
    QcChunkMesh &m_mesh;
    const int m_worldX = 0;
    const int m_worldZ = 0;
};

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
