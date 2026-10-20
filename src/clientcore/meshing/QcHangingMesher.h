#pragma once

#include "clientcore/meshing/QcEntityMesher.h"
#include "clientcore/session/QcClientEntity.h"

// Draws item frames and paintings (decisions E-790, E-791): a frame as a thin board in its item's
// texture with the held item in front, turned by its rotation (a block item as a small cube, any
// other item flat); a painting as one picture tile per block (QcPaintingMotif::tileTexture) on a
// board whose back and edges show its back texture.
namespace QcHangingMesher {

void append(QcEntityMesh &mesh, const QcClientEntity &entity, const QcContentRegistry &content, QcEntityLight light,
            const QcEntityMesher::UvLookup &uv);

} // namespace QcHangingMesher
