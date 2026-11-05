// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/entitymeshes/QcHeldItemMesher.h"

#include "clientcore/entitymeshes/QcMeshRotation.h"

#include <algorithm>
#include <span>
#include <utility>

namespace QcEntityMesher {

namespace {

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
    const SampledPlacement placed(place);
    const QRectF &rect = sprite.rect;
    const QList<QcSpriteShape::FormQuad> &form = sprite.shape.form();
    const std::span<QcEntityVertex> vertices = mesh.addQuads(form.size());
    std::size_t next = 0;
    for (const QcSpriteShape::FormQuad &quad : form) {
        for (const QcSpriteShape::FormCorner &corner : quad.corners) {
            QcEntityVertex vertex = mesh.vertexAt(placed(corner.point * sprite.size));
            vertex.u = static_cast<float>(rect.left() + rect.width() * corner.texture.x());
            vertex.v = static_cast<float>(rect.top() + rect.height() * corner.texture.y());
            vertex.shade = quad.shade;
            vertex.skyLight = sprite.light.sky;
            vertex.blockLight = sprite.light.block;
            vertices[next++] = vertex;
        }
    }
}

QcVec3 spriteGrip(const ItemLook &look, double size)
{
    QPointF unit = defaultGrip(look.grip);
    const std::shared_ptr<const QcSpriteShape> &held = look.gripShape ? look.gripShape : look.shape;
    if (held && !held->isEmpty()) {
        const QcSpriteShape &shape = *held;
        const QPointF texel = look.grip == QcHoldGrip::Middle ? shape.centroid() : shape.handlePoint();
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
