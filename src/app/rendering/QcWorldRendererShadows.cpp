// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The sun shadows of QcWorldRenderer (decision E-903): the shadow map's resources follow the
// quality, the cascades follow the camera, and the shadow step draws the opaque and cut-out lists of
// the sections and the entities into each cascade's tile.
#include "app/rendering/QcWorldRenderer.h"

#include "clientcore/camera/QcFrustum.h"
#include "clientcore/light/QcShadowCascades.h"

#include <QColor>

#include <algorithm>

void QcWorldRenderer::prepareShadows()
{
    const QcRenderOptions::Shadows wanted = m_frame.render.shadows();
    // The pipelines that receive the shadows are built with the bindings of the old map.
    if (m_shadows.replaces(m_device.rhi, wanted)) {
        m_pass.releaseAtlasPipelines();
        m_waterDraw.reset();
        m_water.releaseAtlas();
    }
    // The receivers also read the material maps (decision E-908): without even their flat textures
    // the pipelines are built without receiving.
    const bool ready = m_shadows.prepare(m_device.rhi, wanted) && m_materials.usable(m_device.rhi);
    // Pipelines built for the other case (shadows came or went) are built again.
    if (m_pass.atlasPipelinesTried && m_pass.shadowReceivers != ready) {
        m_pass.releaseAtlasPipelines();
    }
}

void QcWorldRenderer::prepareMaterials(QRhiResourceUpdateBatch *updates)
{
    const bool wanted = m_frame.render.materials().maps;
    m_materials.setAtlasCell(static_cast<int>(m_device.atlasCellPixels));
    // The pipelines with the receivers' bindings and the bindings go before the textures they refer to.
    if (m_materials.replaces(m_device.rhi, wanted)) {
        m_pass.releaseAtlasPipelines();
        m_shadows.releaseAtlasBindings();
    }
    m_materials.prepare(m_device.rhi, wanted, updates);
}

void QcWorldRenderer::uploadShadows(QRhiResourceUpdateBatch *updates, QVector3D drawCamera)
{
    if (!m_shadows.ready()) {
        return;
    }
    const QcRenderOptions::Materials materials = m_frame.render.materials();
    const bool maps = materials.maps && m_materials.hasMaps();
    const QcShadowMap::Frame frame{m_frame.camera.position, m_frame.camera.forward(), drawCamera, QcShadowCascades::light(m_frame.sky),
                                   {maps ? 1.0f : 0.0f, maps && materials.lightHighlights ? 1.0f : 0.0f, 1.0f, 1.0f}};
    m_shadows.upload(updates, frame);
}

void QcWorldRenderer::useCaster(QRhiCommandBuffer *commandBuffer, QRhiGraphicsPipeline *pipeline, QRhiShaderResourceBindings *bindings,
                                QRhiCommandBuffer::DynamicOffset offset)
{
    commandBuffer->setGraphicsPipeline(pipeline);
    commandBuffer->setShaderResources(bindings, 1, &offset);
    setBound({});
}

void QcWorldRenderer::drawShadowMap(QRhiCommandBuffer *commandBuffer, QRhiResourceUpdateBatch *updates)
{
    // Depth only: the colour of the clear is never used.
    commandBuffer->beginPass(m_shadows.target(), Qt::black, {1.0f, 0}, updates);
    if (const std::optional<QcShadowMap::Casters> casters = m_shadows.casters(m_device)) {
        for (int i = 0; i < m_shadows.cascadeCount(); ++i) {
            drawCascade(commandBuffer, *casters, i);
        }
    }
    commandBuffer->endPass();
}

void QcWorldRenderer::drawCascade(QRhiCommandBuffer *commandBuffer, const QcShadowMap::Casters &casters, int index)
{
    // The tiles lie side by side along x, each the full height: the same rectangle whichever corner
    // the backend counts rows from.
    const auto size = static_cast<float>(m_shadows.resolution());
    commandBuffer->setViewport(QRhiViewport(size * static_cast<float>(index), 0.0f, size, size));

    // The sections whose boxes reach into the cascade's box (its matrix is relative to the camera,
    // like the boxes: decision E-883), by page as in cull().
    const QcFrustum::Planes planes = QcFrustum::planes(m_shadows.casterMatrix(index));
    const QcDrawOrigins &origins = m_meshStore.origins();
    const QVector3D drawCamera = m_view.camera;
    m_casters.clear();
    for (const auto &[key, mesh] : m_meshStore.meshes()) {
        if (mesh.opaque.indexCount == 0 && mesh.cutout.indexCount == 0) {
            continue;
        }
        const QVector3D corner = origins.at(mesh.drawOrigin).toVector3D() - drawCamera;
        if (QcFrustum::intersectsBox(planes, corner + mesh.boundsMin, corner + mesh.boundsMax)) {
            m_casters.emplace_back(mesh.slot.page, &mesh);
        }
    }
    std::ranges::sort(m_casters, std::ranges::less{}, &std::pair<quint32, const QcGpuMesh *>::first);

    const QRhiCommandBuffer::DynamicOffset offset = m_shadows.casterOffset(index);
    useCaster(commandBuffer, casters.solid, casters.bindings, offset);
    for (const auto &[page, mesh] : m_casters) {
        drawBuffers(commandBuffer, mesh->opaque, mesh->drawOrigin);
    }
    // Leaves and plants with their alpha test: light falls through the gaps of a crown.
    useCaster(commandBuffer, casters.cutout, casters.bindings, offset);
    for (const auto &[page, mesh] : m_casters) {
        drawBuffers(commandBuffer, mesh->cutout, mesh->drawOrigin);
    }
    // Creatures, players and dropped items; the hand casts nothing (the player's own body is not drawn).
    if (m_device.entityBuffers.indexCount > 0) {
        useCaster(commandBuffer, casters.entities, casters.bindings, offset);
        drawBuffers(commandBuffer, m_device.entityBuffers.list(), m_entityOrigin);
    }
}
