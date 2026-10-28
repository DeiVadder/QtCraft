#include "app/rendering/QcWorldRenderer.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcRenderPipelines.h"
#include "clientcore/camera/QcFrustum.h"
#include "clientcore/light/QcWaterLook.h"
#include "common/core/QcDowncast.h"
#include "common/core/QcWorldConstants.h"
#include "common/diagnostics/QcThreadCpuClock.h"

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

namespace {

// Whether `pipeline` may draw with `bindings`: QRhi wants the layout the pipeline was built with
// (bindings, types and stages). With another one Metal binds the vertex buffers to other slots, and
// the draw reads garbage (decision E-896); such a draw is left out, with one warning.
bool fitsLayout(const QRhiGraphicsPipeline *pipeline, const QRhiShaderResourceBindings *bindings)
{
    if (pipeline->shaderResourceBindings()->isLayoutCompatible(bindings)) {
        return true;
    }
    static bool warned = false;
    if (!warned) {
        warned = true;
        qCWarning(lcRenderer) << "World renderer: bindings without the layout of their pipeline; the draw is left out";
    }
    return false;
}

} // namespace

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
    if (!m_pass.fits(drawTarget())) {
        dropPass();
    }
}

void QcWorldRenderer::replaceDevice()
{
    // The sections on the old device are gone with it: the view has them meshed again.
    m_meshesLost = m_device.rhi != nullptr;
    m_classicPlan = QcFramePlan::selected(QcRenderOptions::Mode::Classic);
    m_realisticPlan = QcFramePlan::selected(QcRenderOptions::Mode::Realistic);
    setPlan(m_classicPlan);
    // The pass group refers to the shadow map's bindings and the scene target: dropped before them.
    dropPass();
    // The water refers to the scene of the post-processing, the shadow map and the atlas.
    m_waterDraw.reset();
    m_water.reset();
    m_post.reset();
    m_shadows.reset();
    m_materials.reset();
    m_baseVertex = rhi()->isFeatureSupported(QRhi::BaseVertex);
    // Test hook QTCRAFT_NO_BASE_INSTANCE=1: the path of GPUs without base instances (decision E-883)
    // on one that has them, for comparison screenshots.
    m_baseInstance = rhi()->isFeatureSupported(QRhi::BaseInstance) && qEnvironmentVariableIntValue("QTCRAFT_NO_BASE_INSTANCE") != 1;
    if (!m_baseInstance) {
        qCInfo(lcRenderer) << "World renderer: draws bind their origin entry (no base instance)";
    }
    m_meshStore.reset(rhi(), plan().sectionLight);
    QcDrawOrigins &origins = m_meshStore.origins();
    m_entityOrigin = origins.add({});
    m_framedMapOrigin = origins.add({});
    m_handOrigin = origins.add({});
    // The pass group first: its pipelines refer to the device's bindings.
    dropPass();
    const QcWorldDeviceResources retired = std::exchange(m_device, QcWorldDeviceResources::create(rhi()));
}

void QcWorldRenderer::dropPass()
{
    // Handed out whole, so its destructor releases it in reverse member order (QcWorldRendererResources.h).
    const QcWorldPassResources retired = std::exchange(m_pass, {});
}

void QcWorldRenderer::choosePlan(QSize outputSize, QRhiResourceUpdateBatch *updates)
{
    const bool wanted = m_frame.render.realistic();
    const bool realistic = wanted && m_post.prepare(m_device.rhi, outputSize, m_frame.render, renderTarget(), updates);
    if (wanted && !realistic && !m_warnedRealistic) {
        qCWarning(lcRenderer) << "World renderer: the realistic mode cannot be drawn on this GPU; drawing the classic one";
        m_warnedRealistic = true;
    }
    // Only a change of the target drops the pass group here; a new output is initialize()'s.
    const QcFramePlan &chosen = realistic ? m_realisticPlan : m_classicPlan;
    const bool targetChanges = chosen.postProcessed() != plan().postProcessed();
    setPlan(chosen);
    if (targetChanges) {
        dropPass();
    }

    // The classic mode holds no post-processing and no shadow map on the GPU (a refused request
    // stays remembered); the pipelines that read them went with the pass group above. The material
    // maps stay while the classic plan only stands in for a wanted realistic one (decision E-912):
    // building them again takes seconds.
    if (realistic) {
        prepareShadows();
        prepareWater();
    } else {
        m_waterDraw.reset();
        m_water.release();
        m_post.release();
        m_shadows.release();
        m_materials.standBy(wanted);
    }
}

QRhiRenderTarget *QcWorldRenderer::drawTarget() const
{
    QRhiRenderTarget *const scene = plan().postProcessed() ? m_post.sceneTarget() : nullptr;
    return scene ? scene : renderTarget();
}

QcPostProcess::Composite QcWorldRenderer::composite(const QMatrix4x4 &viewProjection) const
{
    const QcSkyState &sky = m_frame.sky;
    QcPostProcess::Composite composite;
    composite.viewProjection = viewProjection;
    composite.fogColor = sky.fogColor();
    // Rain brings the classic fog closer (fogScale below 1); the height fog thickens with it. Caves
    // stay clear (FrameInput::outdoors).
    composite.fogDensity = QcPostProcess::fogDensity * m_frame.outdoors / static_cast<float>(std::max(sky.fogScale, 0.25));
    composite.cameraHeight = static_cast<float>(m_frame.camera.position.y);
    composite.fogBase = static_cast<float>(QcWorld::seaLevel) + QcPostProcess::fogBaseAboveSea;
    composite.fogDistance = m_frame.fogEnd;
    composite.sunDirection = sky.sunDirection;
    composite.sunGlow = QcPostProcess::fogSunGlow * static_cast<float>(std::clamp(sky.daylight, 0.0, 1.0));
    composite.exposure = static_cast<float>(m_frame.render.exposure);
    composite.underwater = m_frame.underwater;
    composite.waterColour = QcWaterLook::underwaterColour(sky.daylight);
    return composite;
}

void QcWorldRenderer::synchronize(QQuickRhiItem *item)
{
    // Render thread, GUI thread blocked: the only point where the view is read.
    auto *view = qcDowncast<QcWorldView>(item);
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
    // The maps' pixels are handed over once; the renderer lets them go after their upload. A lost
    // set is reported only while the realistic plan is drawn (the last frame's): without it nothing
    // would upload the maps built again.
    m_materials.offer(m_frame.materials, std::exchange(view->m_pendingMaterials, {}));
    if (m_materials.takeLost(m_frame.render.materials().maps && plan().postProcessed())) {
        view->materialsLostOnDevice();
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

    const qint64 threadCpuNs = QcThreadCpuClock::frameNowNs();
    if (threadCpuNs >= 0 && m_lastThreadCpuNs >= 0) {
        m_sample.renderThreadCpuMs = static_cast<double>(threadCpuNs - m_lastThreadCpuNs) / 1e6;
    }
    m_lastThreadCpuNs = threadCpuNs;
    m_sample.gpuMs = commandBuffer->lastCompletedGpuTime() * 1000.0;
}

void QcWorldRenderer::prepareResources(QRhiResourceUpdateBatch *updates)
{
    // The pipelines with the atlas bindings go before the bindings and the texture they refer to.
    if (m_device.atlasOutdated(m_frame.atlasVersion)) {
        m_pass.releaseAtlasPipelines();
        m_waterDraw.reset();
        m_water.releaseAtlas();
        m_shadows.releaseAtlasBindings();
        m_device.replaceAtlas(m_frame.atlas, m_frame.animationMap, m_frame.atlasVersion, updates);
    }
    if (plan().postProcessed()) {
        prepareMaterials(updates);
    }
    // Without an atlas texture (the GPU refused even the placeholder) the world is not drawn; a
    // refused pipeline is tried again only with a new atlas or pass group.
    if (m_device.hasAtlas() && !m_pass.atlasPipelinesTried) {
        // The realistic mode's pipelines receive the sun shadows while there is a map (decision E-903).
        const QList<QRhiShaderResourceBinding> materials = m_materials.entries();
        QRhiShaderResourceBindings *const receivers =
            plan().postProcessed() && !materials.isEmpty() ? m_shadows.receiverBindings(m_device, materials) : nullptr;
        m_pass.createAtlasPipelines(m_device.rhi, drawTarget(), receivers ? receivers : m_device.atlasBindings(), receivers != nullptr);
    }
    // The water's pipeline before the passes (decision E-906), with the shadow receivers' entries
    // while the other pipelines receive the shadows.
    m_waterDraw.reset();
    if (plan().postProcessed() && m_water.ready()) {
        m_waterDraw = m_water.pipeline(m_device, m_pass.shadowReceivers ? &m_shadows : nullptr, m_post.sceneContinuedTarget());
    }
    if (!m_pass.outlinePipelineTried) {
        m_pass.createOutlinePipeline(m_device.rhi, drawTarget(), m_device.outlineBindings.get());
    }

    QElapsedTimer uploadTime;
    uploadTime.start();
    m_device.uploadOutline(updates, m_frame.selection, m_frame.camera.position);
    m_sample.uploadBytes += m_device.entityBuffers.uploadDynamic(m_device.rhi, updates, m_frame.entityMesh.get());
    m_sample.uploadBytes += m_device.handBuffers.uploadDynamic(m_device.rhi, updates, m_frame.handMesh.get());
    const QcEntityMesh *framedMaps = m_frame.framedMapMesh && m_frame.framedMapTiles ? m_frame.framedMapMesh.get() : nullptr;
    m_sample.uploadBytes += m_device.framedMapBuffers.uploadDynamic(m_device.rhi, updates, framedMaps);
    if (framedMaps && !framedMaps->indices.isEmpty()) {
        m_sample.uploadBytes += m_device.framedMaps.upload(m_device.rhi, updates, *m_frame.framedMapTiles, m_device.frameUniforms.get());
    }
    m_sample.uploadBytes += m_meshStore.uploadQueued(updates);
    m_sample.uploadMs = static_cast<double>(uploadTime.nsecsElapsed()) / 1e6;
}

void QcWorldRenderer::uploadFrameUniforms(QRhiResourceUpdateBatch *updates, const QMatrix4x4 &viewProjection, QVector3D drawCamera,
                                          QSize outputSize) const
{
    FrameUniforms uniforms{};
    std::copy_n(viewProjection.constData(), 16, uniforms.viewProjection);
    // The shaders see positions relative to the camera (decision E-883): the camera stays at the
    // origin of their space, cameraPosition is zero. The block and entity shaders subtract the
    // camera's offset from the render anchor from their draws' origins (QcDrawOrigins).
    uniforms.drawCamera[0] = drawCamera.x();
    uniforms.drawCamera[1] = drawCamera.y();
    uniforms.drawCamera[2] = drawCamera.z();
    const QVector3D fog = m_frame.sky.fogColor();
    uniforms.fogColor[0] = fog.x();
    uniforms.fogColor[1] = fog.y();
    uniforms.fogColor[2] = fog.z();
    uniforms.fogColor[3] = 1.0f;
    // Rain brings the fog closer (QcSkyState::fogScale); the far plane stays where the clear fog ends.
    uniforms.params[0] = m_frame.fogStart * static_cast<float>(m_frame.sky.fogScale);
    uniforms.params[1] = m_frame.fogEnd * static_cast<float>(0.5 + 0.5 * m_frame.sky.fogScale);
    uniforms.params[2] = static_cast<float>(m_frame.sky.daylight);
    // The floor with a blast's flash, kept so that floor and its sky-light share stay at most 1.
    uniforms.params[3] = std::min(minimumLight + static_cast<float>(m_frame.sky.flashLight), 1.0f - QcRenderPipelines::skyLightFloor);
    uniforms.extra[0] = m_device.rhi->isClipDepthZeroToOne() ? 1.0f : 0.0f;
    uniforms.extra[1] = static_cast<float>(outputSize.width());
    uniforms.extra[2] = static_cast<float>(outputSize.height());
    uniforms.extra[3] = skyLightFloor;
    uniforms.atlas[0] = static_cast<float>(m_device.atlasGrid.width());
    uniforms.atlas[1] = static_cast<float>(m_device.atlasGrid.height());
    uniforms.atlas[2] = m_device.atlasCellPixels;
    uniforms.atlas[3] = m_device.atlasCellPixels > 0.0f ? static_cast<float>(m_frame.anisotropicSamples) : 1.0f;
    uniforms.animation[0] = m_frame.animationTicks;
    // Light sources and lava beyond white in the HDR scene of the realistic mode, exactly 1 in the
    // classic one (decision E-899).
    uniforms.animation[1] = plan().glowGain();
    // Moving lights only in the realistic mode, also when it was asked for but the classic one draws
    // (decision E-902).
    const int lightCount = plan().postProcessed() ? m_frame.render.movingLights() : 0;
    const QcPackedLights lights = QcMovingLights::pack(m_frame.movingLights, m_frame.camera.position, lightCount);
    uniforms.animation[2] = static_cast<float>(lights.count);
    for (std::size_t i = 0; i < lights.lights.size(); ++i) {
        std::ranges::copy(lights.lights[i], uniforms.lights[i]);
    }

    updates->updateDynamicBuffer(m_device.frameUniforms.get(), 0, sizeof(FrameUniforms), &uniforms);
}

void QcWorldRenderer::usePipeline(QRhiCommandBuffer *commandBuffer, QRhiGraphicsPipeline *pipeline, QRhiShaderResourceBindings *resources)
{
    commandBuffer->setGraphicsPipeline(pipeline);
    commandBuffer->setShaderResources(resources);
    // QRhi wants the vertex input set again after a pipeline change.
    setBound({});
}

void QcWorldRenderer::drawBuffers(QRhiCommandBuffer *commandBuffer, const QcGpuList &list, quint32 origin, quint32 firstIndex, quint32 count)
{
    if (list.indexCount == 0) {
        return;
    }

    const quint32 drawn = count > 0 ? count : list.indexCount;
    // The draw's origin (decision E-883): its first instance reads the entry, or the origins are
    // bound at the entry.
    const quint32 instance = m_baseInstance ? origin : 0;
    const QRhiCommandBuffer::VertexInput offsets = m_meshStore.origins().inputAt(origin, m_baseInstance);
    if (m_baseVertex) {
        // The buffers from their start, bound only when they change: the lists of a section page
        // differ in first index and base vertex alone (decision E-820).
        const Binding binding{list.vertices, list.indices, list.indexFormat, m_baseInstance ? 0 : origin};
        if (binding.vertices != bound().vertices || binding.indices != bound().indices || binding.indexFormat != bound().indexFormat) {
            const std::array<QRhiCommandBuffer::VertexInput, 2> inputs{QRhiCommandBuffer::VertexInput(list.vertices, 0), offsets};
            commandBuffer->setVertexInput(0, static_cast<int>(inputs.size()), inputs.data(), list.indices, 0, list.indexFormat);
            setBound(binding);
        } else if (binding.origin != bound().origin) {
            // Only the offsets move (no base instance): binding 1 alone. QRhi takes the index buffer
            // with every call, so it is passed again unchanged.
            commandBuffer->setVertexInput(QcDrawOrigins::binding, 1, &offsets, list.indices, 0, list.indexFormat);
            setBound(binding);
        }
        commandBuffer->drawIndexed(drawn, 1, list.firstIndex + firstIndex, list.baseVertex, instance);
    } else {
        const std::array<QRhiCommandBuffer::VertexInput, 2> inputs{QRhiCommandBuffer::VertexInput(list.vertices, list.vertexOffset), offsets};
        commandBuffer->setVertexInput(0, static_cast<int>(inputs.size()), inputs.data(), list.indices, list.indexOffset, list.indexFormat);
        commandBuffer->drawIndexed(drawn, 1, firstIndex, 0, instance);
    }
    ++m_sample.drawCalls;
    m_sample.indicesDrawn += drawn;
}

void QcWorldRenderer::drawTranslucent(QRhiCommandBuffer *commandBuffer, const FrameView &view)
{
    // Back-to-front by section centre so overlapping translucent sections blend correctly; only
    // the sections with translucent faces (or water, when it is drawn here) take part.
    const bool withWater = plan().waterWithTranslucent();
    const QcDrawOrigins &origins = m_meshStore.origins();
    QList<std::pair<float, VisibleSection>> &translucent = m_sorted;
    translucent.clear();
    for (const VisibleSection &section : view.visible) {
        const QcGpuMesh *mesh = section.mesh;
        if (mesh->translucent.indexCount > 0 || (withWater && mesh->water.indexCount > 0)) {
            translucent.append({distanceSquared(*mesh, origins, view.camera), section});
        }
    }
    std::ranges::sort(translucent, std::ranges::greater{}, &std::pair<float, VisibleSection>::first);

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
    for (const auto &[distance, section] : std::as_const(translucent)) {
        const QcGpuMesh *mesh = section.mesh;
        if (withWater && !mesh->translucentRuns.isEmpty()) {
            drawTranslucentRuns(section, use, commandBuffer);
            continue;
        }
        if (withWater && mesh->water.indexCount > 0) {
            use(water);
            drawBuffers(commandBuffer, mesh->water, section.origin);
        }
        if (mesh->translucent.indexCount > 0) {
            use(m_pass.translucentPipeline.get());
            drawBuffers(commandBuffer, mesh->translucent, section.origin);
        }
    }
}

void QcWorldRenderer::drawTranslucentRuns(VisibleSection section, const std::function<void(QRhiGraphicsPipeline *)> &use,
                                          QRhiCommandBuffer *commandBuffer)
{
    const QcGpuMesh &mesh = *section.mesh;
    // Runs alternate between water (even) and the other translucent faces (odd).
    std::array<quint32, 2> next = {0, 0};
    for (qsizetype run = 0; run < mesh.translucentRuns.size(); ++run) {
        const quint32 count = mesh.translucentRuns[run];
        if (count == 0) {
            continue;
        }
        const auto kind = static_cast<std::size_t>(run % 2);
        use(kind == 0 ? waterPipelineInTranslucent() : m_pass.translucentPipeline.get());
        drawBuffers(commandBuffer, kind == 0 ? mesh.water : mesh.translucent, section.origin, next[kind], count);
        next[kind] += count;
    }
}

QRhiGraphicsPipeline *QcWorldRenderer::waterPipelineInTranslucent() const
{
    return m_waterLikeTranslucent ? m_pass.translucentPipeline.get() : m_pass.waterPipeline.get();
}

void QcWorldRenderer::drawWater(QRhiCommandBuffer *commandBuffer, const FrameView &view)
{
    if (!view.water) {
        return;
    }
    const QcDrawOrigins &origins = m_meshStore.origins();
    QList<std::pair<float, VisibleSection>> &water = m_sorted;
    water.clear();
    for (const VisibleSection &section : view.visible) {
        if (section.mesh->water.indexCount > 0) {
            water.append({distanceSquared(*section.mesh, origins, view.camera), section});
        }
    }
    std::ranges::sort(water, std::ranges::greater{}, &std::pair<float, VisibleSection>::first);

    // The realistic water with its own bindings (decision E-906), else the plain one.
    if (m_waterDraw && fitsLayout(m_waterDraw->pipeline, m_waterDraw->bindings)) {
        usePipeline(commandBuffer, m_waterDraw->pipeline, m_waterDraw->bindings);
    } else {
        usePipeline(commandBuffer, m_pass.waterPipeline.get());
    }
    for (const auto &[distance, section] : std::as_const(water)) {
        drawBuffers(commandBuffer, section.mesh->water, section.origin);
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
        drawBuffers(commandBuffer, m_device.handBuffers.list(), m_handOrigin);
    }
}

float QcWorldRenderer::distanceSquared(const QcGpuMesh &mesh, const QcDrawOrigins &origins, QVector3D camera)
{
    // In floats relative to the render anchor: a few hundred blocks at most, plenty for the order.
    return (origins.at(mesh.drawOrigin).toVector3D() + mesh.middle() - camera).lengthSquared();
}

const QcWorldRenderer::FrameView &QcWorldRenderer::cull(const QMatrix4x4 &viewProjection, QVector3D drawCamera,
                                                        QRhiResourceUpdateBatch *updates)
{
    const QcFrustum::Planes planes = QcFrustum::planes(viewProjection);
    FrameView &view = m_view;
    view.visible.clear();
    view.water = false;
    view.belowWorld = m_frame.camera.position.y < 0.0;
    view.camera = drawCamera;
    // The entity meshes and the hand are built around a block near the camera: their entries change
    // when that block does.
    QcDrawOrigins &origins = m_meshStore.origins();
    origins.move(m_entityOrigin, m_device.entityBuffers.origin);
    origins.move(m_framedMapOrigin, m_device.framedMapBuffers.origin);
    origins.move(m_handOrigin, m_device.handBuffers.origin);

    const QcGpuMeshStore::Meshes &meshes = m_meshStore.meshes();
    // Each with its page, so the sort below compares a dense array instead of the scattered meshes.
    m_paged.clear();
    m_paged.reserve(meshes.size());
    for (const auto &[key, mesh] : meshes) {
        if (!mesh.isDrawable(view.belowWorld)) {
            continue;
        }
        // The box relative to the camera: the corner's entry (relative to the render anchor, decision
        // E-883) minus the camera's offset from the anchor, both small floats.
        const QVector3D corner = origins.at(mesh.drawOrigin).toVector3D() - drawCamera;
        if (QcFrustum::intersectsBox(planes, corner + mesh.boundsMin, corner + mesh.boundsMax)) {
            m_paged.emplace_back(mesh.slot.page, &mesh);
            view.water = view.water || mesh.water.indexCount > 0;
        }
    }
    // By page, so the opaque pass binds each page once (decision E-820); the passes sorted by
    // distance keep their own order.
    std::ranges::sort(m_paged, std::ranges::less{}, &std::pair<quint32, const QcGpuMesh *>::first);
    view.visible.reserve(static_cast<qsizetype>(m_paged.size()));
    for (const auto &[page, mesh] : m_paged) {
        view.visible.append({mesh, mesh->drawOrigin});
    }
    m_sample.uploadBytes += origins.upload(m_device.rhi, updates);
    return view;
}

void QcWorldRenderer::drawOpaque(QRhiCommandBuffer *commandBuffer, const FrameView &view)
{
    usePipeline(commandBuffer, m_pass.solidPipeline.get());
    for (const auto &[mesh, origin] : view.visible) {
        drawBuffers(commandBuffer, mesh->opaque, origin);
        if (view.belowWorld) {
            drawBuffers(commandBuffer, mesh->underside, origin);
        }
    }

    // The cut-out faces after all solid ones (decision E-756): with the depth of the solid world
    // written first, most of their hidden fragments fail the depth test before shading too.
    bool cutoutPipelineSet = false;
    for (const auto &[mesh, origin] : view.visible) {
        if (mesh->cutout.indexCount == 0) {
            continue;
        }
        if (!cutoutPipelineSet) {
            usePipeline(commandBuffer, m_pass.cutoutPipeline.get());
            cutoutPipelineSet = true;
        }
        drawBuffers(commandBuffer, mesh->cutout, origin);
    }
}

void QcWorldRenderer::drawEntities(QRhiCommandBuffer *commandBuffer)
{
    if (m_device.entityBuffers.indexCount > 0 && m_pass.entityPipeline) {
        usePipeline(commandBuffer, m_pass.entityPipeline.get());
        drawBuffers(commandBuffer, m_device.entityBuffers.list(), m_entityOrigin);
    }
    // The maps in item frames: the same pipeline with the framed-map texture (decision E-809), whose
    // bindings must have the pipeline's layout (decision E-896): with the receivers' bindings when
    // the pipeline receives the sun shadows (decision E-903).
    QRhiShaderResourceBindings *const framedMaps =
        m_pass.shadowReceivers ? m_shadows.framedMapBindings(m_device, m_materials.entries()) : m_device.framedMaps.bindings();
    if (m_device.framedMapBuffers.indexCount > 0 && m_pass.entityPipeline && framedMaps
        && fitsLayout(m_pass.entityPipeline.get(), framedMaps)) {
        usePipeline(commandBuffer, m_pass.entityPipeline.get(), framedMaps);
        drawBuffers(commandBuffer, m_device.framedMapBuffers.list(), m_framedMapOrigin);
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
    case QcRenderPass::TranslucentBehindWater:
        drawTranslucentBehindWater(commandBuffer, view);
        break;
    case QcRenderPass::Weather:
        m_sample.drawCalls += m_pass.precipitationRenderer.draw(commandBuffer) ? 1 : 0;
        break;
    case QcRenderPass::Overlay:
        m_sample.drawCalls += m_pass.debugLineRenderer.draw(commandBuffer) ? 1 : 0;
        drawOutlineAndHand(commandBuffer);
        break;
    case QcRenderPass::PostProcess:
        m_post.drawComposite(commandBuffer);
        ++m_sample.drawCalls;
        break;
    case QcRenderPass::Shadow:
    case QcRenderPass::Refraction:
        // Drawn into their own targets by drawPlan(): drawShadowMap() into the shadow map,
        // QcWaterSurface::drawCopy() into the water's copy of the scene.
        break;
    }
}

void QcWorldRenderer::drawPlan(QRhiCommandBuffer *commandBuffer, QRhiResourceUpdateBatch *updates, const FrameView &view, QSize outputSize)
{
    const QVector3D horizon = m_frame.sky.horizon;
    const bool ready = m_pass.hasBlockPipelines();
    const QList<QcFramePlan::Group> groups = plan().groups();
    for (const QcFramePlan::Group &group : groups) {
        // The sun's shadow map (decision E-903), drawn only while a light casts shadows; otherwise
        // the uploads go with the next pass.
        if (group.target == QcRenderTarget::ShadowMap) {
            if (ready && m_shadows.ready() && m_shadows.casts()) {
                drawShadowMap(commandBuffer, updates);
                updates = nullptr;
            }
            continue;
        }

        // The copy of the scene the water looks through (decision E-906), in a pass of its own.
        if (group.target == QcRenderTarget::Refraction) {
            if (ready && view.water) {
                m_water.drawCopy(commandBuffer);
            }
            continue;
        }

        // The occlusion and the bloom read the finished scene, the output's composite reads them.
        if (group.passes.contains(QcRenderPass::PostProcess)) {
            if (updates) {
                commandBuffer->resourceUpdate(updates);
                updates = nullptr;
            }
            m_post.drawEffects(commandBuffer);
        }
        // The uploads of the frame go with the first render pass.
        commandBuffer->beginPass(targetOf(group.target), QColor::fromRgbF(horizon.x(), horizon.y(), horizon.z()), {1.0f, 0}, updates);
        updates = nullptr;
        if (ready) {
            commandBuffer->setViewport(QRhiViewport(0, 0, static_cast<float>(outputSize.width()), static_cast<float>(outputSize.height())));
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
    const QSize outputSize = renderTarget()->pixelSize();
    choosePlan(outputSize, updates);
    prepareResources(updates);

    const float aspect = outputSize.height() > 0 ? static_cast<float>(outputSize.width()) / static_cast<float>(outputSize.height()) : 1.0f;
    const float farPlane = m_frame.fogEnd + QcCamera::farPlaneMargin;
    // Positions relative to the camera (decision E-883): the view is the camera's rotation alone,
    // which the sky shares.
    const QMatrix4x4 viewProjection =
        m_device.rhi->clipSpaceCorrMatrix() * m_frame.camera.projectionMatrix(aspect, farPlane) * m_frame.camera.viewRotation();
    const QcVec3 &eye = m_frame.camera.position;
    // After the uploads of the new sections: a new anchor rewrites their entries too.
    const QVector3D drawCamera = m_meshStore.origins().follow(eye);
    uploadFrameUniforms(updates, viewProjection, drawCamera, outputSize);
    if (plan().postProcessed()) {
        uploadShadows(updates, drawCamera);
        uploadWater(updates, viewProjection);
    }
    QRhiRenderTarget *const target = drawTarget();
    m_pass.debugLineRenderer.prepare(m_device.rhi, target, updates, m_device.frameUniforms.get(),
                                     {m_frame.debugLines, m_frame.debugLinesOrigin - eye});
    m_pass.precipitationRenderer.prepare(m_device.rhi, target, updates, m_device.frameUniforms.get(),
                                         {m_frame.precipitation, m_frame.precipitationOrigin - eye});
    m_pass.skyRenderer.prepare(m_device.rhi, target, updates, {viewProjection, m_frame.sky, eye, plan().sunGain()});
    if (plan().postProcessed()) {
        m_post.uploadComposite(updates, composite(viewProjection));
    }

    const FrameView &view = cull(viewProjection, drawCamera, updates);
    m_sample.visibleSections = static_cast<int>(view.visible.size());
    drawPlan(commandBuffer, updates, view, outputSize);

    if (m_frame.collectRenderSamples) {
        m_sample.sections = static_cast<int>(m_meshStore.meshes().size());
        m_sample.sectionBytes = m_meshStore.bytes();
        m_sample.worldRenderMs = static_cast<double>(renderTime.nsecsElapsed()) / 1e6;
        m_samples.append(m_sample);
    }
}
