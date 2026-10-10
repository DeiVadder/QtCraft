#include "QcPreviewScene.h"

#include "QcContentRegistry.h"
#include "QcFigureLook.h"

namespace QcPreviewScene {

QcEntityMesh build(const QcContentRegistry &content, const Figure &figure, const QcEntityMesher::UvLookup &uv)
{
    QcEntityMesh mesh;
    QcEntityMesher::ModelLook look = QcFigureLook::player(content, figure.appearance);
    if (!look.isValid()) {
        return mesh;
    }

    // Open sky at full daylight and no block light: as in the world at noon.
    const QcEntityMesher::Placement placement{{}, figure.yaw, QcEntityLight(1.0f, 0.0f), false};
    const std::optional<QcEntityMesher::ItemLook> held = QcFigureLook::held(content, figure.held);
    const QList<QcEntityMesher::Attachment> armour = QcFigureLook::armour(content, figure.equipment, look);
    QcAnimator::BodyPose body = QcAnimator::animateBody(figure.pose, figure.seconds);
    body.capeLift = figure.cape.lift;
    body.capeSway = figure.cape.sway;
    QcEntityMesher::appendModel(mesh, {look, held ? &*held : nullptr, &armour}, body, placement, uv);
    return mesh;
}

} // namespace QcPreviewScene
