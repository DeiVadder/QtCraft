// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The puffs of QcEntityFrameBuilder: the flash and smoke of explosions and the clouds of ink.

#include "app/rendering/QcEntityFrameBuilder.h"

#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"
#include "clientcore/session/QcExplosionEffects.h"
#include "clientcore/session/QcInkClouds.h"

#include <optional>

void QcEntityFrameBuilder::appendPuff(QcEntityMesh &mesh, const QcEntityMesher::ItemLook &look, const QcEffectPuff &puff, QcEntityLight light,
                                      const QcEntityMesher::UvLookup &uv)
{
    QcEntityMesher::appendBlock(mesh, look, QcEntityMesher::BlockPlacement::centredAt(puff.position, puff.size), light, uv);
}

void QcEntityFrameBuilder::appendExplosions(QcEntityMesh &mesh, const EntityFrame &frame, double ticks)
{
    QcEntityMesher::ItemLook flash;
    flash.cube = true;
    flash.faces.fill(QString::fromLatin1(QcExplosionEffects::flashTexture));
    QcEntityMesher::ItemLook smoke;
    smoke.cube = true;
    smoke.faces.fill(QString::fromLatin1(QcExplosionEffects::smokeTexture));

    for (const QcExplosionEffects::Puff &puff : m_parts.session->worldMirror().extras().explosions().puffs()) {
        if (const std::optional<QcExplosionEffects::Look> look = QcExplosionEffects::lookAt(puff, ticks)) {
            const QcEntityLight light = look->flash ? QcEntityLight(1.0f, 1.0f) : entityLight(frame.world, look->cube.position);
            appendPuff(mesh, look->flash ? flash : smoke, look->cube, light, frame.uv);
        }
    }
}

void QcEntityFrameBuilder::appendInk(QcEntityMesh &mesh, const EntityFrame &frame, double ticks)
{
    QcEntityMesher::ItemLook ink;
    ink.cube = true;
    for (const QcInkClouds::Puff &puff : m_parts.session->worldMirror().inkClouds().puffs()) {
        if (const std::optional<QcEffectPuff> look = QcInkClouds::lookAt(puff, ticks)) {
            ink.faces.fill(puff.texture);
            appendPuff(mesh, ink, *look, entityLight(frame.world, look->position), frame.uv);
        }
    }
}
