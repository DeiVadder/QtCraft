#include "app/rendering/QcWorldRenderer.h"

#include "app/debug/QcAppLogging.h"
#include "app/hooks/QcThreadCpuClock.h"
#include "app/rendering/QcRenderPipelines.h"
#include "clientcore/camera/QcFrustum.h"

#include <QColor>
#include <QElapsedTimer>
#include <QVector4D>

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

using QcRenderPipelines::FrameUniforms;
using QcRenderPipelines::minimumLight;
using QcRenderPipelines::skyLightFloor;

void QcWorldRenderer::initialize(QRhiCommandBuffer *commandBuffer)
{
    Q_UNUSED(commandBuffer);
    // QQuickRhiItem is not functional without a QRhi and does not call initialize() then; without
    // one the renderer keeps nothing and render() draws nothing.
    if (!rhi()) {
        return;
    }

    // A new QRhi: both groups anew. Qt Quick deletes the renderer with the item's scene graph node
    // (another window, an invalidated scene graph), before the QRhi it used goes away, and
    // QQuickRhiItemRenderer has no releaseResources(): the groups are released by the destructor.
    // So m_device.rhi is null on the first call, and a QRhi that differs while the renderer lives
    // is a new device beside a live old one, never a new one at the old one's address.
    if (m_device.rhi != rhi()) {
        replaceDevice();
    }

    // Qt Quick calls initialize() for every new render target (resize, sample count, format); one
    // the pipelines do not fit drops the pass group only, and render() builds it anew.
    if (!m_pass.fits(renderTarget())) {
        dropPass();
    }
}

void QcWorldRenderer::replaceDevice()
{
    // The sections on the old device are gone with it: the view has them meshed again.
    m_meshesLost = m_device.rhi != nullptr;
    m_plan = QcFramePlan::selected();
    m_baseVertex = rhi()->isFeatureSupported(QRhi::BaseVertex);
    m_meshStore.reset(rhi(), m_plan.sectionLight);
    // The pass group first: its pipelines refer to the device's bindings.
    dropPass();
    const QcWorldDeviceResources retired = std::exchange(m_device, QcWorldDeviceResources::create(rhi()));
}

void QcWorldRenderer::dropPass()
{
    // Handed out whole, so its destructor releases it in reverse member order (QcWorldRendererResources.h).
    const QcWorldPassResources retired = std::exchange(m_pass, {});
}

void QcWorldRenderer::synchronize(QQuickRhiItem *item)
{
    // Render thread, GUI thread blocked: the only point where the view is read.
    auto *view = static_cast<QcWorldView *>(item);
    m_frame = view->m_frame;

    if (view->m_clearAllMeshes) {
        view->m_clearAllMeshes = false;
        m_meshStore.clearAll();
    }

    // A section updated several times since the last frame takes only its latest mesh.
    for (QcWorldView::MeshUpdate &update : view->m_pendingMeshUpdates) {
        m_meshStore.queue(update.key, std::move(update.mesh));
    }
    view->m_pendingMeshUpdates.clear();

    if (m_meshesLost) {
        view->meshesLostOnDevice();
        m_meshesLost = false;
    }

    // Handed over: the view takes the samples, the renderer starts an empty list.
    if (m_frame.collectRenderSamples) {
        if (view->m_renderSamples.isEmpty()) {
            view->m_renderSamples.swap(m_samples);
        } else {
            view->m_renderSamples.append(std::move(m_samples));
        }
    }
    m_samples.clear();
}

void QcWorldRenderer::startSample(QRhiCommandBuffer *commandBuffer)
{
    m_sample = {};
    if (!m_frame.collectRenderSamples) {
        m_lastThreadCpuNs = -1;
        return;
    }

    const qint64 threadCpuNs = QcThreadCpuClock::nowNs();
    if (threadCpuNs >= 0 && m_lastThreadCpuNs >= 0) {
        m_sample.renderThreadCpuMs = double(threadCpuNs - m_lastThreadCpuNs) / 1e6;
    }
    m_lastThreadCpuNs = threadCpuNs;
    m_sample.gpuMs = commandBuffer->lastCompletedGpuTime() * 1000.0;
}

void QcWorldRenderer::prepareResources(QRhiResourceUpdateBatch *updates)
{
    // The pipelines with the atlas bindings go before the bindings and the texture they refer to.
    if (m_device.atlasOutdated(m_frame.atlasVersion)) {
        m_pass.releaseAtlasPipelines();
        m_device.replaceAtlas(m_frame.atlas, m_frame.atlasVersion, updates);
    }
    // Without an atlas texture (the GPU refused even the placeholder) the world is not drawn; a
    // refused pipeline is tried again only with a new atlas or pass group.
    if (m_device.atlasTexture && !m_pass.atlasPipelinesTried) {
        m_pass.createAtlasPipelines(m_device.rhi, renderTarget(), m_device.atlasBindings());
    }
    if (!m_pass.outlinePipelineTried) {
        m_pass.createOutlinePipeline(m_device.rhi, renderTarget(), m_device.outlineBindings.get());
    }

    QElapsedTimer uploadTime;
    uploadTime.start();
    m_device.uploadOutline(updates, m_frame.selection);
    m_sample.uploadBytes += m_device.entityBuffers.uploadDynamic(m_device.rhi, updates, m_frame.entityMesh.get());
    m_sample.uploadBytes += m_device.handBuffers.uploadDynamic(m_device.rhi, updates, m_frame.handMesh.get());
    const QcEntityMesh *framedMaps = m_frame.framedMapMesh && m_frame.framedMapTiles ? m_frame.framedMapMesh.get() : nullptr;
    m_sample.uploadBytes += m_device.framedMapBuffers.uploadDynamic(m_device.rhi, updates, framedMaps);
    if (framedMaps && !framedMaps->indices.isEmpty()) {
        m_sample.uploadBytes += m_device.framedMaps.upload(m_device.rhi, updates, *m_frame.framedMapTiles, m_device.frameUniforms.get());
    }
    m_sample.uploadBytes += m_meshStore.uploadQueued(updates);
    m_sample.uploadMs = double(uploadTime.nsecsElapsed()) / 1e6;
}

void QcWorldRenderer::uploadFrameUniforms(QRhiResourceUpdateBatch *updates, const QMatrix4x4 &viewProjection, QSize outputSize) const
{
    FrameUniforms uniforms{};
    std::copy_n(viewProjection.constData(), 16, uniforms.viewProjection);
    uniforms.cameraPosition[0] = m_frame.camera.position.x();
    uniforms.cameraPosition[1] = m_frame.camera.position.y();
    uniforms.cameraPosition[2] = m_frame.camera.position.z();
    const QVector3D fog = m_frame.sky.fogColor();
    uniforms.fogColor[0] = fog.x();
    uniforms.fogColor[1] = fog.y();
    uniforms.fogColor[2] = fog.z();
    uniforms.fogColor[3] = 1.0f;
    // Rain brings the fog closer (QcSkyState::fogScale); the far plane stays where the clear fog ends.
    uniforms.params[0] = m_frame.fogStart * float(m_frame.sky.fogScale);
    uniforms.params[1] = m_frame.fogEnd * float(0.5 + 0.5 * m_frame.sky.fogScale);
    uniforms.params[2] = float(m_frame.sky.daylight);
    uniforms.params[3] = minimumLight;
    uniforms.extra[0] = m_device.rhi->isClipDepthZeroToOne() ? 1.0f : 0.0f;
    uniforms.extra[1] = float(outputSize.width());
    uniforms.extra[2] = float(outputSize.height());
    uniforms.extra[3] = skyLightFloor;
    uniforms.atlas[0] = float(m_device.atlasGrid.width());
    uniforms.atlas[1] = float(m_device.atlasGrid.height());
    uniforms.atlas[2] = m_device.atlasCellPixels;
    uniforms.atlas[3] = m_device.atlasCellPixels > 0.0f ? float(m_frame.anisotropicSamples) : 1.0f;

    updates->updateDynamicBuffer(m_device.frameUniforms.get(), 0, sizeof(FrameUniforms), &uniforms);
}

void QcWorldRenderer::usePipeline(QRhiCommandBuffer *commandBuffer, QRhiGraphicsPipeline *pipeline, QRhiShaderResourceBindings *resources)
{
    commandBuffer->setGraphicsPipeline(pipeline);
    commandBuffer->setShaderResources(resources);
    // QRhi wants the vertex input set again after a pipeline change.
    m_bound = {};
}

void QcWorldRenderer::drawBuffers(QRhiCommandBuffer *commandBuffer, const QcGpuList &list, quint32 firstIndex, quint32 count)
{
    if (list.indexCount == 0) {
        return;
    }

    const quint32 drawn = count > 0 ? count : list.indexCount;
    if (m_baseVertex) {
        // The buffers from their start, bound only when they change: the lists of a section page
        // differ in first index and base vertex alone (decision E-820).
        const Binding binding{list.vertices, list.indices, list.indexFormat};
        if (binding != m_bound) {
            const QRhiCommandBuffer::VertexInput input(list.vertices, 0);
            commandBuffer->setVertexInput(0, 1, &input, list.indices, 0, list.indexFormat);
            m_bound = binding;
        }
        commandBuffer->drawIndexed(drawn, 1, list.firstIndex + firstIndex, list.baseVertex);
    } else {
        const QRhiCommandBuffer::VertexInput input(list.vertices, list.vertexOffset);
        commandBuffer->setVertexInput(0, 1, &input, list.indices, list.indexOffset, list.indexFormat);
        commandBuffer->drawIndexed(drawn, 1, firstIndex);
    }
    ++m_sample.drawCalls;
    m_sample.indicesDrawn += drawn;
}

void QcWorldRenderer::drawTranslucent(QRhiCommandBuffer *commandBuffer, const FrameView &view)
{
    // Back-to-front by section centre so overlapping translucent sections blend correctly; only
    // the sections with translucent faces (or water, when it is drawn here) take part.
    const bool withWater = m_plan.waterWithTranslucent();
    const QVector3D eye = m_frame.camera.position;
    QList<std::pair<float, const QcGpuMesh *>> translucent;
    for (const QcGpuMesh *mesh : view.visible) {
        if (mesh->translucent.indexCount > 0 || (withWater && mesh->water.indexCount > 0)) {
            translucent.append({(mesh->centre() - eye).lengthSquared(), mesh});
        }
    }
    std::ranges::sort(translucent, std::ranges::greater{}, &std::pair<float, const QcGpuMesh *>::first);

    // The pipeline changes only where a section's water and its other faces meet, and only while the
    // water pipeline differs from the translucent one; today they are alike, so the step binds one
    // pipeline for the whole frame.
    QRhiGraphicsPipeline *const water = waterPipelineInTranslucent();
    const QRhiGraphicsPipeline *current = nullptr;
    const std::function<void(QRhiGraphicsPipeline *)> use = [&](QRhiGraphicsPipeline *pipeline) {
        if (pipeline != current) {
            usePipeline(commandBuffer, pipeline);
            current = pipeline;
        }
    };
    for (const auto &[distance, mesh] : std::as_const(translucent)) {
        if (withWater && !mesh->translucentRuns.isEmpty()) {
            drawTranslucentRuns(*mesh, use, commandBuffer);
            continue;
        }
        if (withWater && mesh->water.indexCount > 0) {
            use(water);
            drawBuffers(commandBuffer, mesh->water);
        }
        if (mesh->translucent.indexCount > 0) {
            use(m_pass.translucentPipeline.get());
            drawBuffers(commandBuffer, mesh->translucent);
        }
    }
}

void QcWorldRenderer::drawTranslucentRuns(const QcGpuMesh &mesh, const std::function<void(QRhiGraphicsPipeline *)> &use,
                                          QRhiCommandBuffer *commandBuffer)
{
    // Runs alternate between water (even) and the other translucent faces (odd).
    std::array<quint32, 2> next = {0, 0};
    for (qsizetype run = 0; run < mesh.translucentRuns.size(); ++run) {
        const quint32 count = mesh.translucentRuns[run];
        if (count == 0) {
            continue;
        }
        const auto kind = std::size_t(run % 2);
        use(kind == 0 ? waterPipelineInTranslucent() : m_pass.translucentPipeline.get());
        drawBuffers(commandBuffer, kind == 0 ? mesh.water : mesh.translucent, next[kind], count);
        next[kind] += count;
    }
}

QRhiGraphicsPipeline *QcWorldRenderer::waterPipelineInTranslucent() const
{
    return m_waterLikeTranslucent ? m_pass.translucentPipeline.get() : m_pass.waterPipeline.get();
}

void QcWorldRenderer::drawWater(QRhiCommandBuffer *commandBuffer, const FrameView &view)
{
    const QVector3D eye = m_frame.camera.position;
    QList<std::pair<float, const QcGpuMesh *>> water;
    for (const QcGpuMesh *mesh : view.visible) {
        if (mesh->water.indexCount > 0) {
            water.append({(mesh->centre() - eye).lengthSquared(), mesh});
        }
    }
    std::ranges::sort(water, std::ranges::greater{}, &std::pair<float, const QcGpuMesh *>::first);

    usePipeline(commandBuffer, m_pass.waterPipeline.get());
    for (const auto &[distance, mesh] : std::as_const(water)) {
        drawBuffers(commandBuffer, mesh->water);
    }
}

void QcWorldRenderer::drawOutlineAndHand(QRhiCommandBuffer *commandBuffer)
{
    if (!m_frame.selection.isEmpty() && m_pass.outlinePipeline && m_device.outlineVertexCount > 0) {
        usePipeline(commandBuffer, m_pass.outlinePipeline.get());
        const QRhiCommandBuffer::VertexInput input(m_device.outlineVertices.get(), 0);
        commandBuffer->setVertexInput(0, 1, &input);
        commandBuffer->draw(m_device.outlineVertexCount);
        ++m_sample.drawCalls;
    }
    // Last, with its depth squeezed to the front: the arm never disappears into a wall.
    if (m_device.handBuffers.indexCount > 0 && m_pass.handPipeline) {
        usePipeline(commandBuffer, m_pass.handPipeline.get());
        drawBuffers(commandBuffer, m_device.handBuffers.list());
    }
}

QcWorldRenderer::FrameView QcWorldRenderer::cull(const QMatrix4x4 &viewProjection) const
{
    const QcFrustum::Planes planes = QcFrustum::planes(viewProjection);
    FrameView view;
    view.belowWorld = m_frame.camera.position.y() < 0.0f;
    const QcGpuMeshStore::Meshes &meshes = m_meshStore.meshes();
    // Each with its page, so the sort below compares a dense array instead of the scattered meshes.
    std::vector<std::pair<quint32, const QcGpuMesh *>> paged;
    paged.reserve(meshes.size());
    for (const auto &[key, mesh] : meshes) {
        if (mesh.isDrawable(view.belowWorld) && QcFrustum::intersectsBox(planes, mesh.boundsMin, mesh.boundsMax)) {
            paged.emplace_back(mesh.slot.page, &mesh);
        }
    }
    // By page, so the opaque pass binds each page once (decision E-820); the passes sorted by
    // distance keep their own order.
    std::ranges::sort(paged, std::ranges::less{}, &std::pair<quint32, const QcGpuMesh *>::first);
    view.visible.reserve(qsizetype(paged.size()));
    for (const auto &[page, mesh] : paged) {
        view.visible.append(mesh);
    }
    return view;
}

void QcWorldRenderer::drawOpaque(QRhiCommandBuffer *commandBuffer, const FrameView &view)
{
    usePipeline(commandBuffer, m_pass.solidPipeline.get());
    for (const QcGpuMesh *mesh : view.visible) {
        drawBuffers(commandBuffer, mesh->opaque);
        if (view.belowWorld) {
            drawBuffers(commandBuffer, mesh->underside);
        }
    }

    // The cut-out faces after all solid ones (decision E-756): with the depth of the solid world
    // written first, most of their hidden fragments fail the depth test before shading too.
    bool cutoutPipelineSet = false;
    for (const QcGpuMesh *mesh : view.visible) {
        if (mesh->cutout.indexCount == 0) {
            continue;
        }
        if (!cutoutPipelineSet) {
            usePipeline(commandBuffer, m_pass.cutoutPipeline.get());
            cutoutPipelineSet = true;
        }
        drawBuffers(commandBuffer, mesh->cutout);
    }
}

void QcWorldRenderer::drawEntities(QRhiCommandBuffer *commandBuffer)
{
    if (m_device.entityBuffers.indexCount > 0 && m_pass.entityPipeline) {
        usePipeline(commandBuffer, m_pass.entityPipeline.get());
        drawBuffers(commandBuffer, m_device.entityBuffers.list());
    }
    // The maps in item frames: the same pipeline with the framed-map texture (decision E-809).
    if (m_device.framedMapBuffers.indexCount > 0 && m_pass.entityPipeline && m_device.framedMaps.bindings()) {
        usePipeline(commandBuffer, m_pass.entityPipeline.get(), m_device.framedMaps.bindings());
        drawBuffers(commandBuffer, m_device.framedMapBuffers.list());
    }
}

void QcWorldRenderer::drawPass(QRhiCommandBuffer *commandBuffer, QcRenderPass pass, const FrameView &view)
{
    switch (pass) {
    case QcRenderPass::Sky:
        m_pass.skyRenderer.draw(commandBuffer);
        break;
    case QcRenderPass::Opaque:
        drawOpaque(commandBuffer, view);
        break;
    case QcRenderPass::Entities:
        drawEntities(commandBuffer);
        break;
    case QcRenderPass::Water:
        drawWater(commandBuffer, view);
        break;
    case QcRenderPass::Translucent:
        drawTranslucent(commandBuffer, view);
        break;
    case QcRenderPass::Weather:
        m_sample.drawCalls += m_pass.precipitationRenderer.draw(commandBuffer) ? 1 : 0;
        break;
    case QcRenderPass::Overlay:
        m_sample.drawCalls += m_pass.debugLineRenderer.draw(commandBuffer) ? 1 : 0;
        drawOutlineAndHand(commandBuffer);
        break;
    case QcRenderPass::Shadow:
    case QcRenderPass::PostProcess:
        // Steps of the realistic mode (stage 45); their targets are not drawn yet (drawPlan).
        break;
    }
}

void QcWorldRenderer::drawPlan(QRhiCommandBuffer *commandBuffer, QRhiResourceUpdateBatch *updates, const FrameView &view, QSize outputSize)
{
    const QVector3D horizon = m_frame.sky.horizon;
    const bool ready = m_pass.hasBlockPipelines();
    const QList<QcFramePlan::Group> groups = m_plan.groups();
    for (const QcFramePlan::Group &group : groups) {
        // Shadow maps and the offscreen scene come with the realistic mode (stage 45).
        if (group.target != QcRenderTarget::Output) {
            if (!m_warnedTarget) {
                qCWarning(lcRenderer) << "Render target" << int(group.target) << "is not supported; its steps are skipped";
                m_warnedTarget = true;
            }
            continue;
        }

        // The uploads of the frame go with the first render pass.
        commandBuffer->beginPass(renderTarget(), QColor::fromRgbF(horizon.x(), horizon.y(), horizon.z()), {1.0f, 0}, updates);
        updates = nullptr;
        if (ready) {
            commandBuffer->setViewport(QRhiViewport(0, 0, float(outputSize.width()), float(outputSize.height())));
            for (const QcRenderPass pass : group.passes) {
                drawPass(commandBuffer, pass, view);
            }
        }
        commandBuffer->endPass();
    }

    if (updates) {
        commandBuffer->resourceUpdate(updates);
    }
}

void QcWorldRenderer::render(QRhiCommandBuffer *commandBuffer)
{
    // initialize() found no QRhi.
    if (!m_device.rhi) {
        return;
    }

    QElapsedTimer renderTime;
    renderTime.start();
    startSample(commandBuffer);
    QRhiResourceUpdateBatch *updates = m_device.rhi->nextResourceUpdateBatch();
    prepareResources(updates);

    const QSize outputSize = renderTarget()->pixelSize();
    const float aspect = outputSize.height() > 0 ? float(outputSize.width()) / float(outputSize.height()) : 1.0f;
    const float farPlane = m_frame.fogEnd + QcCamera::farPlaneMargin;
    const QMatrix4x4 viewProjection =
        m_device.rhi->clipSpaceCorrMatrix() * m_frame.camera.projectionMatrix(aspect, farPlane) * m_frame.camera.viewMatrix();
    uploadFrameUniforms(updates, viewProjection, outputSize);
    m_pass.debugLineRenderer.prepare(m_device.rhi, renderTarget(), updates, m_device.frameUniforms.get(), m_frame.debugLines);
    m_pass.precipitationRenderer.prepare(m_device.rhi, renderTarget(), updates, m_device.frameUniforms.get(), m_frame.precipitation);
    // The sky is seen from the camera's position: the same view without the translation.
    QMatrix4x4 viewRotation = m_frame.camera.viewMatrix();
    viewRotation.setColumn(3, QVector4D(0.0f, 0.0f, 0.0f, 1.0f));
    m_pass.skyRenderer.prepare(m_device.rhi, renderTarget(), updates,
                          {m_device.rhi->clipSpaceCorrMatrix() * m_frame.camera.projectionMatrix(aspect, farPlane) * viewRotation, m_frame.sky, m_frame.camera.position});

    const FrameView view = cull(viewProjection);
    m_sample.visibleSections = int(view.visible.size());
    drawPlan(commandBuffer, updates, view, outputSize);

    if (m_frame.collectRenderSamples) {
        m_sample.sections = int(m_meshStore.meshes().size());
        m_sample.sectionBytes = m_meshStore.bytes();
        m_sample.worldRenderMs = double(renderTime.nsecsElapsed()) / 1e6;
        m_samples.append(m_sample);
    }
}
