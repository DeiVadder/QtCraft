#include "clientcore/meshing/QcBlockRenderTable.h"

#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>

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
    return positiveFace(face) ? high(box)[static_cast<std::size_t>(axis)] : low(box)[static_cast<std::size_t>(axis)];
}

SideRect rectOf(const QcAabb &box, QcBlockFace face)
{
    const auto [a, b] = QcBlockRenderTable::axes(face);
    return {static_cast<float>(low(box)[static_cast<std::size_t>(a)]), static_cast<float>(high(box)[static_cast<std::size_t>(a)]),
        static_cast<float>(low(box)[static_cast<std::size_t>(b)]),
            static_cast<float>(high(box)[static_cast<std::size_t>(b)])};
}

// The texture position (u, v) on a face turned by `turns` quarter turns, as the full cube turns its
// face textures (corner k takes the texture corner k + turns): (u, v) -> (1 - v, u) per turn.
std::pair<float, float> turnedUv(float u, float v, std::size_t turns)
{
    for (std::size_t turn = 0; turn < turns % 4; ++turn) {
        const float turnedU = 1.0f - v;
        v = u;
        u = turnedU;
    }
    return {u, v};
}

// `turns`: quarter turns of the face's texture (QcBlockState::faceUvTurns), so a turned shape turns
// its textures with it as the full cube does (an arrow on a repeater points where it sends).
QcBlockRenderTable::Quad makeQuad(const QcAabb &box, QcBlockFace face, const QRectF &uv, bool stretch, std::size_t turns)
{
    QcBlockRenderTable::Quad quad;
    quad.face = face;
    quad.boundary = planeOf(box, face) == (positiveFace(face) ? 1.0 : 0.0);
    quad.rect = rectOf(box, face);

    const auto &template_ = faceCorners[static_cast<std::size_t>(face)];
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
            position[axis] = static_cast<float>(template_[corner][axis] != 0 ? highs[axis] : lows[axis]);
        }

        float u = 0.0f;
        float v = 0.0f;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const int du = c1[axis] - c0[axis];
            const int dv = c3[axis] - c0[axis];
            // Stretched faces measure positions within the box instead of the cell.
            const float along = stretch ? static_cast<float>((position[axis] - lows[axis]) / (highs[axis] - lows[axis])) : position[axis];
            u += static_cast<float>(du) * (along - static_cast<float>(c0[axis]));
            v += static_cast<float>(dv) * (along - static_cast<float>(c0[axis]));
        }

        quad.corners[corner] = position;
        std::tie(u, v) = turnedUv(u, v, turns);
        quad.uvs[corner] = {static_cast<float>(uv.left() + u * uv.width()), static_cast<float>(uv.bottom() - v * uv.height())};
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
struct Shape
{
    QList<QcBlockRenderTable::Quad> quads;
    QList<QcBlockRenderTable::Cross> crosses;
    std::array<QList<SideRect>, 6> cover;
};

// The shape of `state`; boxes without a texture slot of their own use the face textures `faceUv`.
// `lockTextures`: the face textures stay as on the world's faces (QcShapeDefinition::lockTextures).
[[nodiscard]] Shape shapeOf(const QcBlockState &state, const QcBlockDefinition &block, const QcTextureAtlas &atlas,
                            const std::array<QRectF, 6> &faceUv, bool lockTextures)
{
    Shape shape;
    for (qsizetype b = 0; b < state.boxes.size(); ++b) {
        const QcAabb &box = state.boxes[b];
        const QString slot = state.boxTextures.value(b);
        const bool stretch = state.boxStretch.value(b);
        for (const QcBlockFace face : qcAllBlockFaces) {
            if (insideShape(state.boxes, box, face)) {
                continue;
            }
            const QRectF uv = slot.isEmpty() ? faceUv[static_cast<std::size_t>(face)] : atlas.uv(block.slotTexture(slot));
            const std::size_t turns = lockTextures ? 0 : state.faceUvTurns[static_cast<std::size_t>(face)];
            const QcBlockRenderTable::Quad quad = makeQuad(box, face, uv, stretch, turns);
            if (quad.boundary) {
                shape.cover[static_cast<std::size_t>(face)].append(quad.rect);
            }
            shape.quads.append(quad);
        }
    }

    for (qsizetype c = 0; c < state.crosses.size(); ++c) {
        const QcAabb &box = state.crosses[c];
        const QString slot = state.crossTextures.value(c);
        const QRectF uv = slot.isEmpty() ? faceUv[static_cast<std::size_t>(QcBlockFace::North)] : atlas.uv(block.slotTexture(slot));
        shape.crosses.append({{static_cast<float>(box.min.x), static_cast<float>(box.min.y), static_cast<float>(box.min.z),
            static_cast<float>(box.max.x), static_cast<float>(box.max.y), static_cast<float>(box.max.z)}, uv});
    }
    return shape;
}

// `shape` turned by `sixteenths` of a turn (clockwise seen from above) about the vertical axis
// through the cell's centre (a standing sign, decision E-766). Its faces no longer lie on the cell's
// sides: none is hidden by a neighbour, none hides one.
void spin(Shape &shape, int sixteenths)
{
    const double angle = static_cast<double>(sixteenths) * std::numbers::pi / 8.0;
    const auto c = static_cast<float>(std::cos(angle));
    const auto s = static_cast<float>(std::sin(angle));
    for (QcBlockRenderTable::Quad &quad : shape.quads) {
        quad.boundary = false;
        for (std::array<float, 3> &corner : quad.corners) {
            const float x = corner[0] - 0.5f;
            const float z = corner[2] - 0.5f;
            corner[0] = 0.5f + x * c - z * s;
            corner[2] = 0.5f + x * s + z * c;
        }
    }
    for (QList<SideRect> &cover : shape.cover) {
        cover.clear();
    }
}

// Height of a liquid's surface: falls with the flow level; sources and falling liquid are nearly full.
float liquidHeightOf(const QcContentRegistry &content, QcBlockId id, const QcBlockDefinition &block)
{
    const bool falling = content.property(id, u"falling"_s) == u"true"_s;
    const int level = content.property(id, u"level"_s).toInt();
    const float share = falling || block.flow.distance <= 0 ? 1.0f : 1.0f - static_cast<float>(level) / static_cast<float>(block.flow.distance + 1);
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

// Whether a liquid beside the state slopes down into its cell (Entry::liquidMayEnter).
bool liquidMayEnter(const QcBlockState &state, const QcBlockDefinition &block)
{
    if (block.liquid) {
        return false;
    }
    if (!block.solid) {
        return true;
    }
    if (state.fullCube) {
        return false;
    }
    const QList<QcAabb> &boxes = state.collision.isEmpty() ? state.boxes : state.collision;
    return std::ranges::all_of(boxes, [](const QcAabb &box) { return box.max.y < 0.5; });
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
    entry.liquidMayEnter = liquidMayEnter(state, block);
    entry.lightOpacity = state.lightOpacity;
    entry.lightBlockedFaces = state.lightBlockedFaces;
    entry.lightLevel = block.lightLevel;
    entry.material = materialOf(entry);
    for (const QcBlockFace face : qcAllBlockFaces) {
        entry.faceUv[static_cast<std::size_t>(face)] = atlas.uv(block.texture(state.faceSource[static_cast<std::size_t>(face)]));
    }
    entry.faceUvTurns = state.faceUvTurns;

    if (!state.fullCube) {
        Shape shape = shapeOf(state, block, atlas, entry.faceUv, content.shapeOf(block).lockTextures);
        if (state.fineTurns > 0) {
            spin(shape, state.fineTurns);
        }
        entry.quads = std::move(shape.quads);
        entry.crosses = std::move(shape.crosses);
        entry.cover = std::move(shape.cover);
    } else {
        for (const QcBlockFace face : qcAllBlockFaces) {
            entry.cover[static_cast<std::size_t>(face)] = {SideRect{}};
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

bool QcBlockRenderTable::liquidStandsFor(const Entry &self, QcBlockFace face, const Entry &neighbour)
{
    const bool translucentBlock = !self.liquid && self.layer == QcRenderLayer::Translucent;
    const bool translucentLiquid = neighbour.liquid && neighbour.layer == QcRenderLayer::Translucent;
    if (!translucentBlock || !translucentLiquid) {
        return false;
    }
    if (face == QcBlockFace::Up) {
        return true;
    }
    // Over low flowing water the gap down to its surface is wide: the block keeps its bottom.
    static constexpr float tolerance = 1.0e-4f;
    return face == QcBlockFace::Down && neighbour.liquidHeight >= 1.0f - liquidSurfaceDrop - tolerance;
}

bool QcBlockRenderTable::blockStandsFor(const Entry &self, QcBlockFace face, const Entry &neighbour)
{
    const bool side = face != QcBlockFace::Up && face != QcBlockFace::Down;
    const bool translucentLiquid = self.liquid && self.layer == QcRenderLayer::Translucent;
    const bool translucentBlock = !neighbour.liquid && neighbour.layer == QcRenderLayer::Translucent;
    return side && translucentLiquid && translucentBlock;
}

bool QcBlockRenderTable::hides(const Entry &self, SideRect rect, QcBlockFace face, const Entry &neighbour)
{
    if (neighbour.opaque) {
        return true;
    }
    if (self.liquid && neighbour.liquid) {
        return true;
    }
    if (liquidStandsFor(self, face, neighbour)) {
        return true;
    }
    const bool sameTranslucent = neighbour.type == self.type && self.layer == QcRenderLayer::Translucent;
    if (neighbour.layer != QcRenderLayer::Opaque && !sameTranslucent && !blockStandsFor(self, face, neighbour)) {
        return false;
    }
    const QList<SideRect> &covers = neighbour.cover[static_cast<std::size_t>(opposite(face))];
    return std::ranges::any_of(covers, [rect](SideRect cover) { return cover.contains(rect); });
}

QcBlockRenderTable::QcBlockRenderTable(const QcContentRegistry &content, const QcTextureAtlas &atlas)
    : m_lightTable(std::make_shared<const QcLightTable>(content))
{
    m_entries.reserve(content.stateCount());
    for (qsizetype id = 0; id < content.stateCount(); ++id) {
        m_hasLightSources = m_hasLightSources || content.block(static_cast<QcBlockId>(id)).lightLevel > 0;
        m_entries.append(entryOf(content, atlas, static_cast<QcBlockId>(id)));
    }
}

const QcBlockRenderTable::Entry &QcBlockRenderTable::entry(QcBlockId id) const
{
    return static_cast<qsizetype>(id) < m_entries.size() ? m_entries[static_cast<qsizetype>(id)] : m_invisible;
}

QList<QcBlockRenderTable::Quad> QcBlockRenderTable::drawnQuads(const Entry &entry)
{
    if (!entry.fullCube) {
        return entry.quads;
    }

    QList<Quad> quads;
    for (const QcBlockFace face : qcAllBlockFaces) {
        const QRectF &uv = entry.faceUv[static_cast<std::size_t>(face)];
        Quad quad;
        quad.face = face;
        quad.boundary = true;
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const auto &c = faceCorners[static_cast<std::size_t>(face)][corner];
            quad.corners[corner] = {static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2])};
            const bool left = corner == 0 || corner == 3;
            quad.uvs[corner] = {static_cast<float>(left ? uv.left() : uv.right()), static_cast<float>(corner < 2 ? uv.bottom() : uv.top())};
        }
        quads.append(quad);
    }
    return quads;
}
