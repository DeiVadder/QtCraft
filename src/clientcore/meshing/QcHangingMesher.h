// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/meshing/QcEntityMesher.h"
#include "clientcore/session/QcClientEntity.h"

#include <QRectF>

#include <optional>

// Draws item frames and paintings (decisions E-790, E-791): a frame as a thin board in its item's
// texture with the held item in front, turned by its rotation (a block item as a small cube, any
// other item flat); a painting as one picture tile per block (QcPaintingMotif::tileTexture) on a
// board whose back and edges show its back texture. A frame holding a filled map (decision E-806)
// grows to the whole face like in the reference: append() draws only its edges, and the map lies
// on its front as one quad of the framed-map texture (appendFramedMap, QcFramedMapAtlas), so maps
// in neighbouring frames meet without a gap.
namespace QcHangingMesher {

void append(QcEntityMesh &mesh, const QcClientEntity &entity, const QcContentRegistry &content, QcEntityLight light,
            const QcEntityMesher::UvLookup &uv);

// The id of the filled map an item frame shows; unset for anything else.
[[nodiscard]] std::optional<quint32> framedMap(const QcClientEntity &entity, const QcContentRegistry &content);
// Quarter turns clockwise (as seen from the front) a map in a frame at `rotation` (eighths) shows:
// a map turns 90 degrees per step, so the eight steps show four orientations (the reference).
[[nodiscard]] constexpr int mapQuarterTurns(int rotation)
{
    return rotation % 4;
}
// The map of the frame `hanging` on the whole face at the front of its edges: `uv` is its tile in
// the framed-map texture, turned by mapQuarterTurns(hanging.rotation). The picture's north is up on
// a wall.
void appendFramedMap(QcEntityMesh &mesh, const QcHanging &hanging, const QRectF &uv, QcEntityLight light);

} // namespace QcHangingMesher
