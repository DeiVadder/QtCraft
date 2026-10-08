#pragma once

#include "QcChunkMesh.h"
#include "QcClientEntity.h"
#include "QcMobDefinition.h"

#include <QRectF>

#include <array>

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

// Appearance of a dropped item: a small cube with the block's face textures for placeable cube
// blocks, otherwise a sprite of the item texture drawn as two crossed, double-sided quads.
struct ItemLook
{
    bool cube = false;
    // Indexed by QcBlockFace; used for cubes.
    std::array<QString, 6> faces;
    QString sprite;
};

// Edge length of a dropped cube and of a sprite.
inline constexpr double itemCubeSize = 0.25;
inline constexpr double itemSpriteSize = 0.45;

// A dropped item spinning slowly and bobbing; larger stacks show up to three copies. `seconds` is
// the client clock driving the animation.
void appendItem(QcChunkMesh &mesh, const QcClientEntity &entity, const ItemLook &look, double alpha, float light, double seconds,
                const UvLookup &uv);

} // namespace QcEntityMesher
