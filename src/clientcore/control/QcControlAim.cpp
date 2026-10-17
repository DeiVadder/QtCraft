#include "clientcore/control/QcControlAim.h"

#include "clientcore/session/QcClientSession.h"
#include "common/physics/QcPhysics.h"

#include <QtMath>

#include <cmath>

void QcControlAim::lookAt(QcClientSession &session, QcBlockPos block, std::optional<QcBlockFace> face)
{
    // Aim at the block centre, or just inside the centre of the given face.
    QcVec3 point{block.x + 0.5, block.y + 0.5, block.z + 0.5};
    if (face) {
        const QcFaceOffset normal = qcFaceOffset(*face);
        point += QcVec3{normal.dx * 0.49, normal.dy * 0.49, normal.dz * 0.49};
    }

    lookAtPoint(session, point);
}

void QcControlAim::lookAtPoint(QcClientSession &session, const QcVec3 &point)
{
    const QcVec3 eye = session.playerMotion().position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 delta = point - eye;

    QcPlayerInput intent = session.intent();
    intent.yaw = float(qRadiansToDegrees(std::atan2(delta.x, -delta.z)));
    intent.pitch = float(qRadiansToDegrees(std::atan2(delta.y, std::sqrt(delta.x * delta.x + delta.z * delta.z))));
    session.setIntent(intent);
}
