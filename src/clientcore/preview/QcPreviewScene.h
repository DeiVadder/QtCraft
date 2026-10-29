// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/entitymeshes/QcEntityMesh.h"
#include "clientcore/entitymeshes/QcEntityMesher.h"
#include "clientcore/figures/QcCapeSwing.h"
#include "clientcore/figures/QcEntityPose.h"
#include "common/content/mobs/QcAppearance.h"
#include "common/gameplay/QcEquipment.h"

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
    // Worn armour, drawn like on other players.
    QcEquipment equipment{};
    // The cape's swing (QcCapeSwing, kept by the preview from frame to frame).
    QcCapeSwing::Angles cape = {};
    // The shape of the held item's sprite (QcSpriteShapes); null draws it flat.
    std::shared_ptr<const QcSpriteShape> heldShape = nullptr;
};

// Empty when the content has no humanoid model or no outfit to draw.
QcEntityMesh build(const QcContentRegistry &content, const Figure &figure, const QcEntityMesher::UvLookup &uv);

} // namespace QcPreviewScene
