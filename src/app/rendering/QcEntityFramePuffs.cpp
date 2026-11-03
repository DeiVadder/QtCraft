// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The puffs of QcEntityFrameBuilder: the flash and smoke of explosions and the clouds of ink.

#include "app/rendering/QcEntityFrameBuilder.h"

#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"
#include "clientcore/session/QcExplosionEffects.h"
#include "clientcore/session/QcInkClouds.h"

#include <optional>

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
            const QcEntityLight light = look->flash ? QcEntityLight(1.0f, 1.0f) : entityLight(frame.world, look->position);
            QcEntityMesher::appendBlock(mesh, look->flash ? flash : smoke, {look->position, look->size}, light, frame.uv);
        }
    }
}

void QcEntityFrameBuilder::appendInk(QcEntityMesh &mesh, const EntityFrame &frame, double ticks)
{
    QcEntityMesher::ItemLook ink;
    ink.cube = true;
    for (const QcInkClouds::Puff &puff : m_parts.session->worldMirror().inkClouds().puffs()) {
        if (const std::optional<QcInkClouds::Look> look = QcInkClouds::lookAt(puff, ticks)) {
            ink.faces.fill(puff.texture);
            QcEntityMesher::appendBlock(mesh, ink, {look->position, look->size}, entityLight(frame.world, look->position), frame.uv);
        }
    }
}
