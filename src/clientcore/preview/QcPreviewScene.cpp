#include "QcPreviewScene.h"

#include "QcContentRegistry.h"
#include "QcFigureLook.h"

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
    const std::optional<QcEntityMesher::ItemLook> held = QcFigureLook::held(content, figure.held);
    QcEntityMesher::appendModel(mesh, {look, held ? &*held : nullptr}, QcAnimator::animateBody(figure.pose, figure.seconds), placement, uv);
    return mesh;
}

} // namespace QcPreviewScene
