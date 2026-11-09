// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/entitymeshes/QcEntityMesh.h"
#include "common/core/QcAabb.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "common/protocol/QcBeaconMessages.h"

#include <QList>
#include <QRectF>

/*! \brief The beam of a shining beacon (decision E-1143), after the reference game: a white core,
    a square column slowly turning, and around it a wider, faint glow that does not turn, both from
    the beacon up past the top of the world and tinted in each section's colour.

    Both glow by themselves (QcEntityVertex::emissiveLight) and go into meshes of their own: the
    core is drawn like an entity but casts and receives no sun shadow, the glow adds its light
    without writing depth. Only the faces looking out are built; a camera inside a column gets
    none of that column, so the beam does not fill the view from inside. The texture repeats in
    segments of segmentLength blocks, one tile each, and moves up by scrollPerSecond. */
namespace QcBeamMesher {

/*! Half the width of the core, in blocks (the reference's 0.2). */
inline constexpr double halfWidth = 0.2;
/*! Half the width of the glow, in blocks (the reference's 0.25). */
inline constexpr double glowHalfWidth = 0.25;
/*! How strongly the glow adds its colour (the reference draws it with an alpha of 0.125). */
inline constexpr double glowStrength = 0.125;
/*! The column reaches this many blocks above the top of its last section. */
inline constexpr int above = 256;
/*! Degrees the core turns per second (the reference turns it 2.25 degrees a tick). */
inline constexpr double turnPerSecond = 45.0;
/*! Blocks of beam per tile of its texture. */
inline constexpr double segmentLength = 2.0;
/*! Blocks the texture moves up per second. */
inline constexpr double scrollPerSecond = 1.0;
/*! Room kept between the camera and a column's corners, in blocks: a camera closer than that to
    a column's axis is inside it (more than the near plane). */
inline constexpr double cameraClearance = 0.1;
/*! The texture of the beam. */
inline constexpr auto texture = "qtcraft:beacon_beam";

/*! Appends to \a mesh the core of \a beam, turned and scrolled as at \a seconds of the animations'
    time, in the atlas \a tile of the texture; nothing when \a camera is inside the core. */
void appendCore(QcEntityMesh &mesh, const QcMsg::BeaconBeam &beam, const QcVec3 &camera, double seconds, const QRectF &tile);
/*! Appends to \a mesh the glow around \a beam, scrolled as at \a seconds, in the atlas \a tile;
    nothing when \a camera is inside the glow. */
void appendGlow(QcEntityMesh &mesh, const QcMsg::BeaconBeam &beam, const QcVec3 &camera, double seconds, const QRectF &tile);

/*! Returns the height of \a beam from the beacon's cell to its end, in blocks: its sections and
    the blocks above. */
[[nodiscard]] int heightOf(const QcMsg::BeaconBeam &beam);
/*! Returns the box around the glow of \a beam, which holds its core too, for culling. */
[[nodiscard]] QcAabb boxOf(const QcMsg::BeaconBeam &beam);

} // namespace QcBeamMesher
