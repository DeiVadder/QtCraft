#include "clientcore/meshing/QcHeldItemMesher.h"

#include "clientcore/meshing/QcMeshRotation.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace QcEntityMesher {

namespace {

// Light of the faces by the direction they face in the sprite's space: front and back like a flat
// sprite, the top brightest, the underside darkest.
constexpr float faceShade = 0.95f;
constexpr float topShade = 1.0f;
constexpr float sideShade = 0.75f;
constexpr float bottomShade = 0.55f;

QcEntityLight glowing(QcEntityLight light, const ItemLook &look)
{
    return {light.sky, std::max(light.block, look.glow)};
}

// An affine map sampled from a placement at the origin and the three unit points: the placements
// of held items (fist, arm, bone and model turns, scales and offsets) are affine, so four calls of
// the chain of placement functions stand for the hundreds a sprite's side quads would make.
class SampledPlacement
{
public:
    explicit SampledPlacement(const PlaceFunction &place)
        : m_origin(place({0.0, 0.0, 0.0}))
        , m_x(place({1.0, 0.0, 0.0}) - m_origin)
        , m_y(place({0.0, 1.0, 0.0}) - m_origin)
        , m_z(place({0.0, 0.0, 1.0}) - m_origin)
    {
    }

    [[nodiscard]] QcVec3 operator()(const QcVec3 &p) const { return m_origin + m_x * p.x + m_y * p.y + m_z * p.z; }

private:
    QcVec3 m_origin;
    QcVec3 m_x;
    QcVec3 m_y;
    QcVec3 m_z;
};

// Emits the quads of an extruded sprite; local points are texel coordinates (x from the left, y
// from the top, 0..size of the shape) and z in texels from the middle.
class Extruder
{
public:
    Extruder(QcEntityMesh &mesh, const ExtrudedSprite &sprite, const PlaceFunction &place)
        : m_mesh(mesh)
        , m_sprite(sprite)
        , m_place(place)
        , m_texels(sprite.shape.size())
        , m_scale(sprite.size / double(m_texels))
    {
    }

    void faces() const
    {
        const double n = m_texels;
        const QcVec3 outward{0.0, 0.0, 1.0};
        for (const double side : {0.5, -0.5}) {
            const std::array<QcVec3, 4> corners = {{{0.0, n, side}, {n, n, side}, {n, 0.0, side}, {0.0, 0.0, side}}};
            addFace(corners, outward * side, faceShade, std::nullopt);
        }
    }

    void sides() const
    {
        using Facing = QcSpriteShape::Facing;
        for (const QcSpriteShape::Side &side : m_sprite.shape.sides()) {
            const int x = side.texel.x();
            const int y = side.texel.y();
            switch (side.facing) {
            case Facing::Left:
                addSide(side.texel, {x, y, x, y + 1}, {-1.0, 0.0, 0.0}, sideShade);
                break;
            case Facing::Right:
                addSide(side.texel, {x + 1, y, x + 1, y + 1}, {1.0, 0.0, 0.0}, sideShade);
                break;
            case Facing::Up:
                addSide(side.texel, {x, y, x + 1, y}, {0.0, 1.0, 0.0}, topShade);
                break;
            case Facing::Down:
                addSide(side.texel, {x, y + 1, x + 1, y + 1}, {0.0, -1.0, 0.0}, bottomShade);
                break;
            }
        }
    }

private:
    // An edge from (x0, y0) to (x1, y1) in texel coordinates.
    struct Edge
    {
        int x0;
        int y0;
        int x1;
        int y1;
    };

    void addSide(QPoint texel, Edge edge, const QcVec3 &outward, float shade) const
    {
        const std::array<QcVec3, 4> corners = {{{double(edge.x0), double(edge.y0), -0.5},
                                                {double(edge.x1), double(edge.y1), -0.5},
                                                {double(edge.x1), double(edge.y1), 0.5},
                                                {double(edge.x0), double(edge.y0), 0.5}}};
        addFace(corners, outward, shade, texel);
    }

    // Texel coordinates (y down) -> the sprite's local space (y up, centred on x).
    QcVec3 local(const QcVec3 &t) const { return {(t.x - m_texels * 0.5) * m_scale, (m_texels - t.y) * m_scale, t.z * m_scale}; }

    // A quad of texel-space corners facing `outward` (in the local space, y up): counter-clockwise
    // seen from outside like every entity face. A side takes the colour at its texel's centre, a
    // face the whole picture.
    void addFace(std::array<QcVec3, 4> corners, const QcVec3 &outward, float shade, std::optional<QPoint> texel) const
    {
        // Texel space has y down, so the winding is checked in the local space.
        const QcVec3 a = local(corners[1]) - local(corners[0]);
        const QcVec3 b = local(corners[2]) - local(corners[0]);
        const QcVec3 normal{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        if (normal.dot(outward) < 0.0) {
            std::swap(corners[1], corners[3]);
        }

        const QRectF &rect = m_sprite.rect;
        std::array<QcEntityVertex, 4> quad;
        for (std::size_t i = 0; i < quad.size(); ++i) {
            const QcVec3 world = m_place(local(corners[i]));
            const QPointF at = texel ? QPointF(texel->x() + 0.5, texel->y() + 0.5) : QPointF(corners[i].x, corners[i].y);
            QcEntityVertex &vertex = quad[i];
            vertex.x = float(world.x);
            vertex.y = float(world.y);
            vertex.z = float(world.z);
            vertex.u = float(rect.left() + rect.width() * at.x() / m_texels);
            vertex.v = float(rect.top() + rect.height() * at.y() / m_texels);
            vertex.shade = shade;
            vertex.skyLight = m_sprite.light.sky;
            vertex.blockLight = m_sprite.light.block;
        }
        m_mesh.addQuad(quad);
    }

    QcEntityMesh &m_mesh;
    const ExtrudedSprite &m_sprite;
    SampledPlacement m_place;
    int m_texels;
    double m_scale;
};

// The grip in the sprite's unit square (x from the left, y up from the bottom), without a shape.
QPointF defaultGrip(QcHoldGrip grip)
{
    switch (grip) {
    case QcHoldGrip::Handle:
        return {3.5 / 16.0, 3.5 / 16.0};
    case QcHoldGrip::Middle:
        return {0.5, 0.5};
    case QcHoldGrip::Base:
        break;
    }
    return {0.5, 2.5 / 16.0};
}

// The middle of a block's boxes (0..1 per block), the middle of the block for a full cube.
QcVec3 boxesCentre(const QList<QcAabb> &boxes)
{
    if (boxes.isEmpty()) {
        return {0.5, 0.5, 0.5};
    }
    QcAabb bounds = boxes.constFirst();
    for (const QcAabb &box : boxes) {
        bounds.min = {std::min(bounds.min.x, box.min.x), std::min(bounds.min.y, box.min.y), std::min(bounds.min.z, box.min.z)};
        bounds.max = {std::max(bounds.max.x, box.max.x), std::max(bounds.max.y, box.max.y), std::max(bounds.max.z, box.max.z)};
    }
    return (bounds.min + bounds.max) * 0.5;
}

} // namespace

void appendExtrudedSprite(QcEntityMesh &mesh, const ExtrudedSprite &sprite, const PlaceFunction &place)
{
    if (sprite.shape.isEmpty()) {
        return;
    }
    const Extruder extruder(mesh, sprite, place);
    extruder.faces();
    extruder.sides();
}

QcVec3 spriteGrip(const ItemLook &look, double size)
{
    QPointF unit = defaultGrip(look.grip);
    if (look.shape && !look.shape->isEmpty()) {
        const QcSpriteShape &shape = *look.shape;
        const QPointF texel = look.grip == QcHoldGrip::Middle ? shape.centroid() : QPointF(shape.handle()) + QPointF(0.5, 0.5);
        unit = {texel.x() / shape.size(), 1.0 - texel.y() / shape.size()};
    }
    return {(unit.x() - 0.5) * size, unit.y() * size, 0.0};
}

void HeldItemPainter::paint(const ItemLook &look, const HeldPlacement &placement, const PlaceFunction &place) const
{
    const Turn turn(placement.rotate.y);
    const Rotate rotate(placement.rotate.x, placement.rotate.z);
    const QcVec3 &scale = placement.scale;
    // A point of the item in units around the grip -> the fist's frame.
    const auto fist = [&](const QcVec3 &unit) { return place(placement.translate + rotate(turn({unit.x * scale.x, unit.y * scale.y, unit.z * scale.z}))); };

    if (look.cube) {
        // The item cube is drawn itemCubeSize across, centred: one unit per block edge, held by
        // the middle of its boxes.
        const QcVec3 middle = boxesCentre(look.boxes) - QcVec3{0.5, 0.5, 0.5};
        appendPlacedItem(m_mesh, look, [&](const QcVec3 &p) { return fist(p * (1.0 / itemCubeSize) - middle); }, m_light, m_uv);
        return;
    }

    const QcVec3 grip = spriteGrip(look, 1.0);
    if (look.shape && !look.shape->isEmpty()) {
        appendExtrudedSprite(m_mesh, {*look.shape, m_uv(look.sprite), 1.0, glowing(m_light, look)}, [&](const QcVec3 &p) { return fist(p - grip); });
        return;
    }
    // A flat sprite is drawn itemSpriteSize across.
    appendPlacedItem(m_mesh, look, [&](const QcVec3 &p) { return fist(p * (1.0 / itemSpriteSize) - grip); }, m_light, m_uv);
}

} // namespace QcEntityMesher
