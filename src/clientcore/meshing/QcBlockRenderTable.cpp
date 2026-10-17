#include "clientcore/meshing/QcBlockRenderTable.h"

#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

using Entry = QcBlockRenderTable::Entry;
using SideRect = QcBlockRenderTable::SideRect;

// Corners of each cube face, counter-clockwise seen from outside, as 0/1 per axis. Corners 0 and 1
// map to the bottom edge of the texture, 2 and 3 to the top edge.
constexpr std::array<std::array<std::array<int, 3>, 4>, 6> faceCorners = {{
    {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}}, // East
    {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}}, // West
    {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}}, // Up
    {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}}, // Down
    {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}}, // South
    {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}}, // North
}};

QcBlockFace opposite(QcBlockFace face)
{
    switch (face) {
    case QcBlockFace::East:
        return QcBlockFace::West;
    case QcBlockFace::West:
        return QcBlockFace::East;
    case QcBlockFace::Up:
        return QcBlockFace::Down;
    case QcBlockFace::Down:
        return QcBlockFace::Up;
    case QcBlockFace::South:
        return QcBlockFace::North;
    case QcBlockFace::North:
        return QcBlockFace::South;
    }
    return face;
}

int normalAxis(QcBlockFace face)
{
    const QcFaceOffset offset = qcFaceOffset(face);
    return offset.dx != 0 ? 0 : (offset.dy != 0 ? 1 : 2);
}

bool positiveFace(QcBlockFace face)
{
    const QcFaceOffset offset = qcFaceOffset(face);
    return offset.dx + offset.dy + offset.dz > 0;
}

std::array<double, 3> low(const QcAabb &box)
{
    return {box.min.x, box.min.y, box.min.z};
}

std::array<double, 3> high(const QcAabb &box)
{
    return {box.max.x, box.max.y, box.max.z};
}

// Plane position of a box face along its normal axis.
double planeOf(const QcAabb &box, QcBlockFace face)
{
    const int axis = normalAxis(face);
    return positiveFace(face) ? high(box)[std::size_t(axis)] : low(box)[std::size_t(axis)];
}

SideRect rectOf(const QcAabb &box, QcBlockFace face)
{
    const auto [a, b] = QcBlockRenderTable::axes(face);
    return {float(low(box)[std::size_t(a)]), float(high(box)[std::size_t(a)]), float(low(box)[std::size_t(b)]),
            float(high(box)[std::size_t(b)])};
}

QcBlockRenderTable::Quad makeQuad(const QcAabb &box, QcBlockFace face, const QRectF &uv, bool stretch)
{
    QcBlockRenderTable::Quad quad;
    quad.face = face;
    quad.boundary = planeOf(box, face) == (positiveFace(face) ? 1.0 : 0.0);
    quad.rect = rectOf(box, face);

    const auto &template_ = faceCorners[std::size_t(face)];
    const std::array<double, 3> lows = low(box);
    const std::array<double, 3> highs = high(box);

    // Texture coordinates follow the position on the full cell face, so partial faces show the
    // matching part of the texture.
    const auto &c0 = template_[0];
    const auto &c1 = template_[1];
    const auto &c3 = template_[3];

    for (std::size_t corner = 0; corner < 4; ++corner) {
        std::array<float, 3> position{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            position[axis] = float(template_[corner][axis] != 0 ? highs[axis] : lows[axis]);
        }

        float u = 0.0f;
        float v = 0.0f;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const int du = c1[axis] - c0[axis];
            const int dv = c3[axis] - c0[axis];
            // Stretched faces measure positions within the box instead of the cell.
            const float along = stretch ? float((position[axis] - lows[axis]) / (highs[axis] - lows[axis])) : position[axis];
            u += float(du) * (along - float(c0[axis]));
            v += float(dv) * (along - float(c0[axis]));
        }

        quad.corners[corner] = position;
        quad.uvs[corner] = {float(uv.left() + u * uv.width()), float(uv.bottom() - v * uv.height())};
    }

    return quad;
}

// Whether the face `face` of `box` lies against another box of the same state, inside the shape.
bool insideShape(const QList<QcAabb> &boxes, const QcAabb &box, QcBlockFace face)
{
    const SideRect rect = rectOf(box, face);
    const double plane = planeOf(box, face);
    return std::ranges::any_of(boxes, [&](const QcAabb &other) {
        return &other != &box && planeOf(other, opposite(face)) == plane && rectOf(other, opposite(face)).contains(rect);
    });
}

// Quads, side covers and crosses of a state that is not the full cube.
void addShape(Entry &entry, const QcBlockState &state, const QcBlockDefinition &block, const QcTextureAtlas &atlas)
{
    for (qsizetype b = 0; b < state.boxes.size(); ++b) {
        const QcAabb &box = state.boxes[b];
        const QString slot = state.boxTextures.value(b);
        const bool stretch = state.boxStretch.value(b);
        for (const QcBlockFace face : qcAllBlockFaces) {
            if (insideShape(state.boxes, box, face)) {
                continue;
            }
            const QRectF uv = slot.isEmpty() ? entry.faceUv[std::size_t(face)] : atlas.uv(block.slotTexture(slot));
            const QcBlockRenderTable::Quad quad = makeQuad(box, face, uv, stretch);
            if (quad.boundary) {
                entry.cover[std::size_t(face)].append(quad.rect);
            }
            entry.quads.append(quad);
        }
    }

    for (qsizetype c = 0; c < state.crosses.size(); ++c) {
        const QcAabb &box = state.crosses[c];
        const QString slot = state.crossTextures.value(c);
        const QRectF uv = slot.isEmpty() ? entry.faceUv[std::size_t(QcBlockFace::North)] : atlas.uv(block.slotTexture(slot));
        entry.crosses.append({{float(box.min.x), float(box.min.y), float(box.min.z), float(box.max.x), float(box.max.y), float(box.max.z)}, uv});
    }
}

// Height of a liquid's surface: falls with the flow level; sources and falling liquid are nearly full.
float liquidHeightOf(const QcContentRegistry &content, QcBlockId id, const QcBlockDefinition &block)
{
    const bool falling = content.property(id, u"falling"_s) == u"true"_s;
    const int level = content.property(id, u"level"_s).toInt();
    const float share = falling || block.flow.distance <= 0 ? 1.0f : 1.0f - float(level) / float(block.flow.distance + 1);
    return (1.0f - QcBlockRenderTable::liquidSurfaceDrop) * std::clamp(share, 0.0f, 1.0f);
}

// Liquids first (the water list and its later pipeline), then light sources, then the layer.
QcSurfaceMaterial materialOf(const Entry &entry)
{
    if (entry.liquid) {
        return entry.layer == QcRenderLayer::Translucent ? QcSurfaceMaterial::Water : QcSurfaceMaterial::Liquid;
    }
    if (entry.lightLevel > 0) {
        return QcSurfaceMaterial::Emissive;
    }
    switch (entry.layer) {
    case QcRenderLayer::Translucent:
        return QcSurfaceMaterial::Translucent;
    case QcRenderLayer::Cutout:
        return QcSurfaceMaterial::Cutout;
    case QcRenderLayer::Invisible:
    case QcRenderLayer::Opaque:
        break;
    }
    return QcSurfaceMaterial::Solid;
}

Entry entryOf(const QcContentRegistry &content, const QcTextureAtlas &atlas, QcBlockId id)
{
    const QcBlockState &state = content.state(id);
    const QcBlockDefinition &block = content.block(id);
    Entry entry;
    entry.layer = block.renderLayer;
    entry.type = state.type;
    entry.opaque = state.opaque;
    entry.liquid = block.liquid;
    if (block.liquid) {
        entry.liquidHeight = liquidHeightOf(content, id, block);
    }
    entry.fullCube = state.fullCube;
    entry.lightOpacity = state.lightOpacity;
    entry.lightBlockedFaces = state.lightBlockedFaces;
    entry.lightLevel = block.lightLevel;
    entry.material = materialOf(entry);
    for (const QcBlockFace face : qcAllBlockFaces) {
        entry.faceUv[std::size_t(face)] = atlas.uv(block.texture(state.faceSource[std::size_t(face)]));
    }

    if (!state.fullCube) {
        addShape(entry, state, block, atlas);
    } else {
        for (const QcBlockFace face : qcAllBlockFaces) {
            entry.cover[std::size_t(face)] = {SideRect{}};
        }
    }
    return entry;
}

} // namespace

std::pair<int, int> QcBlockRenderTable::axes(QcBlockFace face)
{
    const int axis = normalAxis(face);
    return {axis == 0 ? 1 : 0, axis == 2 ? 1 : 2};
}

bool QcBlockRenderTable::hides(const Entry &self, SideRect rect, QcBlockFace face, const Entry &neighbour)
{
    if (neighbour.opaque) {
        return true;
    }
    if (self.liquid && neighbour.liquid) {
        return true;
    }
    const bool sameTranslucent = neighbour.type == self.type && self.layer == QcRenderLayer::Translucent;
    if (neighbour.layer != QcRenderLayer::Opaque && !sameTranslucent) {
        return false;
    }
    const QList<SideRect> &covers = neighbour.cover[std::size_t(opposite(face))];
    return std::ranges::any_of(covers, [rect](SideRect cover) { return cover.contains(rect); });
}

QcBlockRenderTable::QcBlockRenderTable(const QcContentRegistry &content, const QcTextureAtlas &atlas)
    : m_lightTable(std::make_shared<const QcLightTable>(content))
{
    m_entries.reserve(content.stateCount());
    for (qsizetype id = 0; id < content.stateCount(); ++id) {
        m_hasLightSources = m_hasLightSources || content.block(QcBlockId(id)).lightLevel > 0;
        m_entries.append(entryOf(content, atlas, QcBlockId(id)));
    }
}

const QcBlockRenderTable::Entry &QcBlockRenderTable::entry(QcBlockId id) const
{
    return qsizetype(id) < m_entries.size() ? m_entries[qsizetype(id)] : m_invisible;
}
