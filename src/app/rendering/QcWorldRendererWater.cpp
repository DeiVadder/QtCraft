// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The water of QcWorldRenderer in the realistic mode (decision E-906): the copy of the scene follows
// the post-processing's scene, the water's uniforms follow the sky, the camera and the quality, and
// the steps after the copy draw on over the scene.
#include "app/rendering/QcWorldRenderer.h"

#include "clientcore/light/QcShadowCascades.h"
#include "clientcore/light/QcWaterLook.h"

#include <algorithm>
#include <utility>

void QcWorldRenderer::prepareWater()
{
    // Without the copy (Low GPUs without float targets, a refused part) the plain water pipeline
    // draws the water.
    if (!m_water.prepare(m_device.rhi, m_post, m_frame.render.water())) {
        m_waterDraw.reset();
    }
}

void QcWorldRenderer::uploadWater(QRhiResourceUpdateBatch *updates, const QMatrix4x4 &viewProjection) const
{
    if (!m_water.ready()) {
        return;
    }
    QcWaterFrame frame;
    frame.viewProjection = viewProjection;
    frame.sky = m_frame.sky;
    frame.light = QcShadowCascades::light(m_frame.sky);
    frame.camera = m_frame.camera.position;
    frame.seconds = m_frame.waterSeconds;
    frame.depthZeroToOne = m_post.depthZeroToOne();
    // Texture rows follow the clip y where framebuffer and NDC point the same way (decision E-902).
    frame.textureYFollowsNdc = m_device.rhi->isYUpInFramebuffer() == m_device.rhi->isYUpInNDC();
    frame.size = m_water.behind()->pixelSize();
    frame.water = m_frame.render.water();
    m_water.upload(updates, QcWaterLook::uniforms(frame));
}

QRhiRenderTarget *QcWorldRenderer::targetOf(QcRenderTarget target) const
{
    switch (target) {
    case QcRenderTarget::Scene:
        return m_post.sceneTarget();
    case QcRenderTarget::SceneContinued:
        return m_post.sceneContinuedTarget();
    case QcRenderTarget::ShadowMap:
        return m_shadows.target();
    // The copy is drawn by QcWaterSurface::drawCopy into its own target.
    case QcRenderTarget::Refraction:
    case QcRenderTarget::Output:
        break;
    }
    return renderTarget();
}

void QcWorldRenderer::drawTranslucentBehindWater(QRhiCommandBuffer *commandBuffer, const FrameView &view)
{
    // Only for the water's copy: without water in view nothing looks through it.
    if (!view.water) {
        return;
    }
    const QcDrawOrigins &origins = m_meshStore.origins();
    QList<std::pair<float, VisibleSection>> &covered = m_sorted;
    covered.clear();
    for (const VisibleSection &section : view.visible) {
        const QcGpuMesh *mesh = section.mesh;
        if (mesh->translucent.indexCount > 0 && (m_frame.underwater || mesh->water.indexCount > 0)) {
            covered.append({distanceSquared(*mesh, origins, view.camera), section});
        }
    }
    if (covered.isEmpty()) {
        return;
    }
    std::ranges::sort(covered, std::ranges::greater{}, &std::pair<float, VisibleSection>::first);
    usePipeline(commandBuffer, m_pass.translucentPipeline.get());
    for (const auto &[distance, section] : std::as_const(covered)) {
        drawBuffers(commandBuffer, section.mesh->translucent, section.origin);
    }
}
