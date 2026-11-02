// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/entitymeshes/QcEntityMesher.h"

#include <utility>

// Items in a hand (decision E-831): sprites are drawn with sides one texel thick around their
// opaque texels (QcSpriteShape), the fist closes around the grip the shape gives, and the content's
// placement (ItemLook::firstPerson, thirdPerson) puts the item around it.

namespace QcEntityMesher {

/*! \brief A sprite with thickness: \c {size} metres across, in the local space of a flat sprite
    (x across and centred, y up from 0 to \c {size}), one texel thick along z and centred on z =
    0. */
struct ExtrudedSprite
{
    /*! The sprite's opaque texels. */
    const QcSpriteShape &shape;
    /*! Atlas rectangle of the sprite's texture. */
    QRectF rect;
    /*! Width and height in metres. */
    double size = itemSpriteSize;
    /*! Light the sprite is drawn in. */
    QcEntityLight light;
};

/*! Appends to \a mesh the front and back of \a sprite as whole quads, each point placed by
    \a place (clear texels fall to the alpha test) and a side quad, in the colour of its texel,
    on every edge between an opaque texel and a clear one. */
void appendExtrudedSprite(QcEntityMesh &mesh, const ExtrudedSprite &sprite, const PlaceFunction &place);

/*! Returns where a hand holds the sprite of \a look drawn \a size metres across, in the sprite's
    local space: the shape's handle (QcHoldGrip::Handle, Base) or the middle of its opaque texels
    (QcHoldGrip::Middle); without a shape, where a handle drawn from the lower left usually is. */
QcVec3 spriteGrip(const ItemLook &look, double size);

/*! \brief Draws held items, in the first-person fist and in a figure's fist.

    The painter keeps its own copy of the UV lookup, so it may be built from a temporary lambda
    and outlive it. */
class HeldItemPainter
{
public:
    HeldItemPainter(QcEntityMesh &mesh, UvLookup uv, QcEntityLight light)
        : m_mesh(mesh)
        , m_uv(std::move(uv))
        , m_light(light)
    {
    }

    /*! \a look placed by \a placement around its grip; \a place maps the fist's frame (for the
        first person the camera's at rest, for a figure its arm's model space) to world space.
        Sprites with a shape are extruded, without one flat; blocks are drawn from their boxes. */
    void paint(const ItemLook &look, const HeldPlacement &placement, const PlaceFunction &place) const;

private:
    QcEntityMesh &m_mesh;
    UvLookup m_uv;
    QcEntityLight m_light;
};

} // namespace QcEntityMesher
