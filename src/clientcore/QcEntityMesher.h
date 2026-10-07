#pragma once

#include "QcChunkMesh.h"
#include "QcClientEntity.h"
#include "QcMobDefinition.h"

#include <QRectF>

#include <functional>

// Builds the triangles of entity models (boxes from the content definitions), placed at the
// interpolated position and turned by yaw. Rebuilt every frame; entities are few.
namespace QcEntityMesher {

using UvLookup = std::function<QRectF(const QString &texture)>;

// Model used for other players, which have no content definition.
const QList<QcMobPart> &playerModel();

// `alpha` interpolates between the previous and current server state; `light` is the sky light
// at the entity in [0, 1].
void appendEntity(QcChunkMesh &mesh, const QcClientEntity &entity, const QList<QcMobPart> &parts, double alpha, float light,
                  const UvLookup &uv);

} // namespace QcEntityMesher
