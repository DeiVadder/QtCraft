#pragma once

#include "QcAppearance.h"
#include "QcEntityMesh.h"
#include "QcEntityMesher.h"
#include "QcEntityPose.h"

class QcContentRegistry;

// The triangles of the character preview: the player's figure meshed exactly as the world meshes
// other players (QcFigureLook, QcAnimator, QcEntityMesher::appendModel), standing at the origin in
// full daylight, so the preview shows what other players see.
namespace QcPreviewScene {

struct Figure
{
    QcAppearance appearance;
    // In the right hand; qcNoItem for none.
    QcItemId held = qcNoItem;
    QcEntityPose pose;
    // QcPreviewCamera::modelYaw().
    double yaw = 0.0;
    // Free-running clock for the idle motion (QcAnimator::animateBody).
    double seconds = 0.0;
};

// Empty when the content has no humanoid model or no outfit to draw.
QcEntityMesh build(const QcContentRegistry &content, const Figure &figure, const QcEntityMesher::UvLookup &uv);

} // namespace QcPreviewScene
