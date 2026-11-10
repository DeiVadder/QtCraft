// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The beacon beams of QcEntityFrameBuilder (decision E-1143).

#include "app/rendering/QcEntityFrameBuilder.h"

#include "app/rendering/QcWorldView.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/entitymeshes/QcBeamMesher.h"
#include "clientcore/session/QcClientSession.h"
#include "clientcore/session/QcClientWorld.h"
#include "common/content/registry/QcContentRegistry.h"

QcEntityFrameBuilder::BeamMeshes QcEntityFrameBuilder::buildBeams(const EntityFrame &frame, const std::optional<QcEntityCulling::ViewPlanes> &planes) const
{
    BeamMeshes meshes{std::make_shared<QcEntityMesh>(), std::make_shared<QcEntityMesh>()};
    meshes.core->origin = meshOrigin();
    meshes.glow->origin = meshes.core->origin;
    const QcVec3 camera = m_parts.view ? m_parts.view->camera().position : m_parts.session->playerMotion().position;
    const QRectF tile = frame.uv(QString::fromLatin1(QcBeamMesher::texture));

    // The beams come and go with their chunks, so only those of loaded chunks are drawn; one whose
    // beacon the player just broke waits for the server's word, but is not drawn meanwhile.
    const QHash<QcBlockPos, QList<QcBeamSection>> &beams = m_parts.session->worldMirror().extras().beams();
    for (auto it = beams.cbegin(); it != beams.cend(); ++it) {
        const std::optional<QcBlockId> block = frame.world.blockAt(it.key());
        if (!block || !frame.content.isValidBlock(*block) || !frame.content.block(*block).opensBeacon) {
            continue;
        }
        const QcMsg::BeaconBeam beam{it.key(), it.value()};
        if (!QcEntityCulling::mayBeSeen(planes, QcBeamMesher::boxOf(beam))) {
            continue;
        }
        QcBeamMesher::appendCore(*meshes.core, beam, camera, frame.seconds, tile);
        QcBeamMesher::appendGlow(*meshes.glow, beam, camera, frame.seconds, tile);
    }
    return meshes;
}
