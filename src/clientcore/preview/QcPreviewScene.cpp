// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/preview/QcPreviewScene.h"

#include "clientcore/figures/QcFigureLook.h"
#include "common/content/registry/QcContentRegistry.h"

namespace QcPreviewScene {

QcEntityMesh build(const QcContentRegistry &content, const Figure &figure, const QcEntityMesher::UvLookup &uv)
{
    QcEntityMesh mesh;
    const QcEntityMesher::ModelLook look = QcFigureLook::player(content, figure.appearance);
    if (!look.isValid()) {
        return mesh;
    }

    // Open sky at full daylight and no block light: as in the world at noon.
    const QcEntityMesher::Placement placement{{}, figure.yaw, QcEntityLight(1.0f, 0.0f), false};
    std::optional<QcEntityMesher::ItemLook> held = QcFigureLook::held(content, figure.held);
    if (held && !held->cube) {
        held->shape = figure.heldShape;
    }
    const QcFigureLook::Dressed dressed = QcFigureLook::armour(content, figure.equipment, look);
    QcAnimator::BodyPose body = QcAnimator::animateBody(figure.pose, figure.seconds);
    body.capeLift = figure.cape.lift;
    body.capeSway = figure.cape.sway;
    QcEntityMesher::appendModel(mesh, {dressed.look, held ? &*held : nullptr, &dressed.armour}, body, placement, uv);
    return mesh;
}

} // namespace QcPreviewScene
