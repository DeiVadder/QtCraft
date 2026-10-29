// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/meshing/QcEntityMesher.h"

#include <utility>

// Items in a hand (decision E-831): sprites are drawn with sides one texel thick around their
// opaque texels (QcSpriteShape), the fist closes around the grip the shape gives, and the content's
// placement (ItemLook::firstPerson, thirdPerson) puts the item around it.
namespace QcEntityMesher {

// A sprite with thickness: `size` metres across, in the local space of a flat sprite (x across and
// centred, y up from 0 to `size`), one texel thick along z and centred on z = 0.
struct ExtrudedSprite
{
    const QcSpriteShape &shape;
    QRectF rect;
    double size = itemSpriteSize;
    QcEntityLight light;
};

// The front and back of the sprite as whole quads (clear texels fall to the alpha test) and a side
// quad, in the colour of its texel, on every edge between an opaque texel and a clear one.
void appendExtrudedSprite(QcEntityMesh &mesh, const ExtrudedSprite &sprite, const PlaceFunction &place);

// Where a hand holds the sprite of `look` drawn `size` metres across, in the sprite's local space:
// the shape's handle (QcHoldGrip::Handle, Base) or the middle of its opaque texels
// (QcHoldGrip::Middle); without a shape, where a handle drawn from the lower left usually is.
QcVec3 spriteGrip(const ItemLook &look, double size);

// Draws held items, in the first-person fist and in a figure's fist. The painter keeps its own
// copy of the UV lookup, so it may be built from a temporary lambda and outlive it.
class HeldItemPainter
{
public:
    HeldItemPainter(QcEntityMesh &mesh, UvLookup uv, QcEntityLight light)
        : m_mesh(mesh)
        , m_uv(std::move(uv))
        , m_light(light)
    {
    }

    // `look` placed by `placement` around its grip; `place` maps the fist's frame (for the first
    // person the camera's at rest, for a figure its arm's model space) to world space. Sprites with
    // a shape are extruded, without one flat; blocks are drawn from their boxes.
    void paint(const ItemLook &look, const HeldPlacement &placement, const PlaceFunction &place) const;

private:
    QcEntityMesh &m_mesh;
    UvLookup m_uv;
    QcEntityLight m_light;
};

} // namespace QcEntityMesher
