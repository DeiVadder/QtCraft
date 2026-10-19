#include "app/rendering/QcWorldRenderer.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcAtlasTexture.h"

#include "app/hooks/QcThreadCpuClock.h"
#include "app/rendering/QcRenderPipelines.h"
#include "app/rendering/QcWorldView.h"
#include "clientcore/camera/QcFrustum.h"

#include <QElapsedTimer>

#include <algorithm>
#include <array>
#include <cstddef>

using namespace Qt::StringLiterals;
using QcRenderPipelines::FrameUniforms;
using QcRenderPipelines::loadShader;
using QcRenderPipelines::minimumLight;

namespace {

constexpr int maxOutlineBoxes = 16;

// 12 edges as line segments of two corners per box.
using OutlineVertices = std::array<float, std::size_t(maxOutlineBoxes) * 24 * 3>;

void appendBoxEdges(const QcAabb &box, OutlineVertices &vertices, std::size_t &next)
{
    // Slightly larger than the boxes so the lines are not hidden by their faces.
    constexpr float grow = 0.004f;
    constexpr std::array<std::pair<int, int>, 12> edges = {
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};

    const float x0 = float(box.min.x) - grow;
    const float y0 = float(box.min.y) - grow;
    const float z0 = float(box.min.z) - grow;
    const float x1 = float(box.max.x) + grow;
    const float y1 = float(box.max.y) + grow;
    const float z1 = float(box.max.z) + grow;
    const std::array<std::array<float, 3>, 8> corners = {{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1},
                                                          {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}};

    for (const auto &[from, to] : edges) {
        for (const int corner : {from, to}) {
            for (const float value : corners[std::size_t(corner)]) {
                vertices[next++] = value;
            }
        }
    }
}

} // namespace

bool QcWorldRenderer::PassResources::fits(const QRhiRenderPassDescriptor *target) const
{
    return renderPass && renderPass->isCompatible(target);
}

void QcWorldRenderer::initialize(QRhiCommandBuffer *commandBuffer)
{
    Q_UNUSED(commandBuffer);
    if (m_rhi != rhi()) {
        m_plan = QcFramePlan::selected();
        m_meshStore.reset(rhi(), m_plan.sectionLight);
        m_pass = {};
        m_outlineBindings.reset();
        m_outlineVertices.reset();
        m_uploadedSelection.reset();
        m_entityBuffers = {};
        m_handBuffers = {};
        m_bindings.reset();
        m_atlasTexture.reset();
        m_atlasSampler.reset();
        m_frameUniforms.reset();
        m_uploadedAtlasVersion = 0;
        m_atlasTried = false;
        m_rhi = rhi();
    }

    if (!m_frameUniforms) {
        m_frameUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(FrameUniforms)));
        m_frameUniforms->create();
        m_atlasSampler = QcAtlasTexture::createSampler(m_rhi);
    }

    if (!m_pass.fits(renderTarget()->renderPassDescriptor())) {
        m_pass = {};
    }
}

void QcWorldRenderer::synchronize(QQuickRhiItem *item)
{
    auto *view = static_cast<QcWorldView *>(item);
    m_camera = view->m_camera;
    m_selection = view->m_selection;
    m_entityMesh = view->m_entityMesh;
    m_handMesh = view->m_handMesh;
    m_debugLines = view->m_debugLines;
    m_precipitation = view->m_precipitation;
    m_sky = view->m_sky;
    m_fogStart = view->m_fogStart;
    m_fogEnd = view->m_fogEnd;

    if (view->m_atlasVersion != m_atlasVersion) {
        m_atlasVersion = view->m_atlasVersion;
        m_atlasLevels = view->m_atlas;
    }
    if (view->m_clearAllMeshes) {
        view->m_clearAllMeshes = false;
        m_meshStore.clearAll();
    }

    // A section updated several times since the last frame takes only its latest mesh.
    for (QcWorldView::MeshUpdate &update : view->m_pendingMeshUpdates) {
        m_meshStore.queue(update.key, std::move(update.mesh));
    }
    view->m_pendingMeshUpdates.clear();

    m_collectSamples = view->m_collectRenderSamples;
    if (m_collectSamples) {
        view->m_renderSamples.append(m_samples);
    }
    m_samples.clear();
}

void QcWorldRenderer::ensureAtlasTexture(QRhiResourceUpdateBatch *updates)
{
    // Tried once per atlas: a texture the GPU refused is not asked for again every frame.
    if (m_atlasTried && m_uploadedAtlasVersion == m_atlasVersion) {
        return;
    }
    m_atlasTried = true;
    QList<QImage> levels = m_atlasLevels;
    if (levels.isEmpty() || levels.front().isNull()) {
        QImage placeholder(16, 16, QImage::Format_RGBA8888);
        placeholder.fill(Qt::magenta);
        levels = {placeholder};
    }

    // A new texture for every atlas (a pack switch changes its size and levels); the bindings and
    // pipelines refer to it.
    m_atlasTexture = QcAtlasTexture::create(m_rhi, levels, updates);
    m_bindings.reset();
    m_pass.opaquePipeline.reset();
    m_pass.translucentPipeline.reset();
    m_pass.waterPipeline.reset();
    m_pass.entityPipeline.reset();
    m_pass.handPipeline.reset();
    m_uploadedAtlasVersion = m_atlasVersion;
}

void QcWorldRenderer::ensureBindings()
{
    if (m_bindings) {
        return;
    }

    m_bindings.reset(m_rhi->newShaderResourceBindings());
    m_bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_frameUniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  m_atlasTexture.get(), m_atlasSampler.get()),
    });
    m_bindings->create();
}

std::unique_ptr<QRhiGraphicsPipeline> QcWorldRenderer::createBlockPipeline(const QShader &vertexShader, const QShader &fragmentShader,
                                                                          const QRhiVertexInputLayout &inputLayout, bool translucent)
{
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(m_rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, vertexShader}, {QRhiShaderStage::Fragment, fragmentShader}});
    pipeline->setVertexInputLayout(inputLayout);
    pipeline->setShaderResourceBindings(m_bindings.get());
    pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    pipeline->setSampleCount(renderTarget()->sampleCount());
    pipeline->setDepthTest(true);
    pipeline->setDepthWrite(!translucent);
    pipeline->setFrontFace(QRhiGraphicsPipeline::CCW);
    pipeline->setCullMode(translucent ? QRhiGraphicsPipeline::None : QRhiGraphicsPipeline::Back);

    if (translucent) {
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        pipeline->setTargetBlends({blend});
    }

    if (!pipeline->create()) {
        qCCritical(lcRenderer) << "Cannot create graphics pipeline";
        return std::unique_ptr<QRhiGraphicsPipeline>();
    }
    return pipeline;
}

void QcWorldRenderer::createPipelines()
{
    ensureBindings();

    const QShader vertexShader = loadShader(u":/shaders/block.vert.qsb"_s, lcRenderer());
    const QShader fragmentShader = loadShader(u":/shaders/block.frag.qsb"_s, lcRenderer());
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{sizeof(QcBlockVertex)}});
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3, quint32(offsetof(QcBlockVertex, x))},
        {0, 1, QRhiVertexInputAttribute::Float2, quint32(offsetof(QcBlockVertex, u))},
        {0, 2, QRhiVertexInputAttribute::UNormByte4, quint32(offsetof(QcBlockVertex, light))},
    });
    // The opaque pass writes alpha 1 (decision E-332): mip levels give cut-out texels an alpha
    // between 0 and 1, which a screenshot or a transparent target would read as half transparent. A
    // colour write mask without alpha did the same but cost up to a third more GPU time.
    const QShader opaqueFragmentShader = loadShader(u":/shaders/block_opaque.frag.qsb"_s, lcRenderer());
    m_pass.opaquePipeline = createBlockPipeline(vertexShader, opaqueFragmentShader, inputLayout, false);
    m_pass.translucentPipeline = createBlockPipeline(vertexShader, fragmentShader, inputLayout, true);
    m_pass.waterPipeline = createBlockPipeline(vertexShader, fragmentShader, inputLayout, true);

    const QShader entityFragment = loadShader(u":/shaders/entity.frag.qsb"_s, lcRenderer());
    m_pass.entityPipeline = QcRenderPipelines::createEntityPipeline(m_rhi, renderTarget(), m_bindings.get(),
                                                               loadShader(u":/shaders/entity.vert.qsb"_s, lcRenderer()), entityFragment);
    m_pass.handPipeline = QcRenderPipelines::createEntityPipeline(m_rhi, renderTarget(), m_bindings.get(), loadShader(u":/shaders/hand.vert.qsb"_s, lcRenderer()),
                                                             entityFragment);
    m_pass.renderPass.reset(renderTarget()->renderPassDescriptor()->newCompatibleRenderPassDescriptor());
}

void QcWorldRenderer::createOutlinePipeline()
{
    if (!m_outlineBindings) {
        m_outlineBindings.reset(m_rhi->newShaderResourceBindings());
        m_outlineBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_frameUniforms.get())});
        m_outlineBindings->create();
        // 12 edges as line segments per box.
        m_outlineVertices.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                                 quint32(std::size_t(maxOutlineBoxes) * 24 * 3 * sizeof(float))));
        m_outlineVertices->create();
        m_uploadedSelection.reset();
    }

    std::unique_ptr<QRhiGraphicsPipeline> pipeline(m_rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, loadShader(u":/shaders/outline.vert.qsb"_s, lcRenderer())},
                               {QRhiShaderStage::Fragment, loadShader(u":/shaders/outline.frag.qsb"_s, lcRenderer())}});

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{3 * sizeof(float)}});
    inputLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0}});
    pipeline->setVertexInputLayout(inputLayout);
    pipeline->setTopology(QRhiGraphicsPipeline::Lines);
    pipeline->setShaderResourceBindings(m_outlineBindings.get());
    pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    pipeline->setSampleCount(renderTarget()->sampleCount());
    pipeline->setDepthTest(true);
    pipeline->setDepthWrite(false);

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    pipeline->setTargetBlends({blend});

    if (pipeline->create()) {
        m_pass.outlinePipeline = std::move(pipeline);
    } else {
        qCCritical(lcRenderer) << "Cannot create outline pipeline";
    }
}

void QcWorldRenderer::uploadOutline(QRhiResourceUpdateBatch *updates)
{
    if (m_selection.isEmpty() || m_selection == m_uploadedSelection || !m_outlineVertices) {
        return;
    }

    OutlineVertices vertices{};
    std::size_t next = 0;
    for (qsizetype b = 0; b < std::min<qsizetype>(m_selection.size(), maxOutlineBoxes); ++b) {
        appendBoxEdges(m_selection[b], vertices, next);
    }

    updates->updateDynamicBuffer(m_outlineVertices.get(), 0, quint32(next * sizeof(float)), vertices.data());
    m_outlineVertexCount = quint32(next / 3);
    m_uploadedSelection = m_selection;
}

void QcWorldRenderer::uploadEntityMesh(QRhiResourceUpdateBatch *updates, const QcEntityMesh *mesh, QcGpuBuffers &buffers)
{
    buffers.indexCount = 0;
    if (!mesh || mesh->indices.isEmpty()) {
        return;
    }

    const auto vertexBytes = quint32(mesh->vertices.size() * qsizetype(sizeof(QcEntityVertex)));
    const auto indexBytes = quint32(mesh->indices.size() * qsizetype(sizeof(quint32)));

    // Dynamic buffers grow on demand and are reused every frame.
    if (!buffers.vertices || buffers.vertices->size() < vertexBytes) {
        buffers.vertices.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, vertexBytes * 2));
        buffers.vertices->create();
    }
    if (!buffers.indices || buffers.indices->size() < indexBytes) {
        buffers.indices.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, indexBytes * 2));
        buffers.indices->create();
    }

    updates->updateDynamicBuffer(buffers.vertices.get(), 0, vertexBytes, mesh->vertices.constData());
    updates->updateDynamicBuffer(buffers.indices.get(), 0, indexBytes, mesh->indices.constData());
    m_sample.uploadBytes += vertexBytes + indexBytes;
    buffers.indexCount = quint32(mesh->indices.size());
}

void QcWorldRenderer::startSample(QRhiCommandBuffer *commandBuffer)
{
    m_sample = {};
    if (!m_collectSamples) {
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
    ensureAtlasTexture(updates);
    // Without an atlas texture (the GPU refused even the placeholder) the world is not drawn.
    if (m_atlasTexture && (!m_pass.opaquePipeline || !m_pass.translucentPipeline || !m_pass.waterPipeline)) {
        createPipelines();
    }
    if (!m_pass.outlinePipeline) {
        createOutlinePipeline();
    }

    QElapsedTimer uploadTime;
    uploadTime.start();
    uploadOutline(updates);
    uploadEntityMesh(updates, m_entityMesh.get(), m_entityBuffers);
    uploadEntityMesh(updates, m_handMesh.get(), m_handBuffers);
    m_sample.uploadBytes += m_meshStore.uploadQueued(updates);
    m_sample.uploadMs = double(uploadTime.nsecsElapsed()) / 1e6;
}

void QcWorldRenderer::uploadFrameUniforms(QRhiResourceUpdateBatch *updates, const QMatrix4x4 &viewProjection, QSize outputSize)
{
    FrameUniforms uniforms{};
    std::copy_n(viewProjection.constData(), 16, uniforms.viewProjection);
    uniforms.cameraPosition[0] = m_camera.position.x();
    uniforms.cameraPosition[1] = m_camera.position.y();
    uniforms.cameraPosition[2] = m_camera.position.z();
    const QVector3D fog = m_sky.fogColor();
    uniforms.fogColor[0] = fog.x();
    uniforms.fogColor[1] = fog.y();
    uniforms.fogColor[2] = fog.z();
    uniforms.fogColor[3] = 1.0f;
    // Rain brings the fog closer (QcSkyState::fogScale); the far plane stays where the clear fog ends.
    uniforms.params[0] = m_fogStart * float(m_sky.fogScale);
    uniforms.params[1] = m_fogEnd * float(0.5 + 0.5 * m_sky.fogScale);
    uniforms.params[2] = float(m_sky.daylight);
    uniforms.params[3] = minimumLight;
    uniforms.extra[0] = m_rhi->isClipDepthZeroToOne() ? 1.0f : 0.0f;
    uniforms.extra[1] = float(outputSize.width());
    uniforms.extra[2] = float(outputSize.height());

    updates->updateDynamicBuffer(m_frameUniforms.get(), 0, sizeof(FrameUniforms), &uniforms);
}

void QcWorldRenderer::drawBuffers(QRhiCommandBuffer *commandBuffer, const QcGpuBuffers &buffers, quint32 firstIndex, quint32 count)
{
    if (buffers.indexCount == 0) {
        return;
    }

    const quint32 drawn = count > 0 ? count : buffers.indexCount;
    const QRhiCommandBuffer::VertexInput input(buffers.vertices.get(), 0);
    commandBuffer->setVertexInput(0, 1, &input, buffers.indices.get(), 0, buffers.indexFormat);
    commandBuffer->drawIndexed(drawn, 1, firstIndex);
    ++m_sample.drawCalls;
    m_sample.indicesDrawn += drawn;
}

void QcWorldRenderer::drawTranslucent(QRhiCommandBuffer *commandBuffer, const FrameView &view)
{
    // Back-to-front by section centre so overlapping translucent sections blend correctly; only
    // the sections with translucent faces (or water, when it is drawn here) take part.
    const bool withWater = m_plan.waterWithTranslucent();
    const QVector3D eye = m_camera.position;
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
            commandBuffer->setGraphicsPipeline(pipeline);
            commandBuffer->setShaderResources();
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
    const QVector3D eye = m_camera.position;
    QList<std::pair<float, const QcGpuMesh *>> water;
    for (const QcGpuMesh *mesh : view.visible) {
        if (mesh->water.indexCount > 0) {
            water.append({(mesh->centre() - eye).lengthSquared(), mesh});
        }
    }
    std::ranges::sort(water, std::ranges::greater{}, &std::pair<float, const QcGpuMesh *>::first);

    commandBuffer->setGraphicsPipeline(m_pass.waterPipeline.get());
    commandBuffer->setShaderResources();
    for (const auto &[distance, mesh] : std::as_const(water)) {
        drawBuffers(commandBuffer, mesh->water);
    }
}

void QcWorldRenderer::drawOutlineAndHand(QRhiCommandBuffer *commandBuffer)
{
    if (!m_selection.isEmpty() && m_pass.outlinePipeline && m_outlineVertexCount > 0) {
        commandBuffer->setGraphicsPipeline(m_pass.outlinePipeline.get());
        commandBuffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput input(m_outlineVertices.get(), 0);
        commandBuffer->setVertexInput(0, 1, &input);
        commandBuffer->draw(m_outlineVertexCount);
        ++m_sample.drawCalls;
    }
    // Last, with its depth squeezed to the front: the arm never disappears into a wall.
    if (m_handBuffers.indexCount > 0 && m_pass.handPipeline) {
        commandBuffer->setGraphicsPipeline(m_pass.handPipeline.get());
        commandBuffer->setShaderResources();
        drawBuffers(commandBuffer, m_handBuffers);
    }
}

QcWorldRenderer::FrameView QcWorldRenderer::cull(const QMatrix4x4 &viewProjection) const
{
    const QcFrustum::Planes planes = QcFrustum::planes(viewProjection);
    FrameView view;
    view.belowWorld = m_camera.position.y() < 0.0f;
    const QcGpuMeshStore::Meshes &meshes = m_meshStore.meshes();
    view.visible.reserve(qsizetype(meshes.size()));
    for (const auto &[key, mesh] : meshes) {
        const bool drawable = mesh.opaque.indexCount > 0 || mesh.translucent.indexCount > 0 || mesh.water.indexCount > 0 || view.belowWorld;
        if (drawable && QcFrustum::intersectsBox(planes, mesh.boundsMin, mesh.boundsMax)) {
            view.visible.append(&mesh);
        }
    }
    return view;
}

void QcWorldRenderer::drawOpaque(QRhiCommandBuffer *commandBuffer, const FrameView &view)
{
    commandBuffer->setGraphicsPipeline(m_pass.opaquePipeline.get());
    commandBuffer->setShaderResources();
    for (const QcGpuMesh *mesh : view.visible) {
        drawBuffers(commandBuffer, mesh->opaque);
        if (view.belowWorld) {
            drawBuffers(commandBuffer, mesh->underside);
        }
    }
}

void QcWorldRenderer::drawEntities(QRhiCommandBuffer *commandBuffer)
{
    if (m_entityBuffers.indexCount > 0 && m_pass.entityPipeline) {
        commandBuffer->setGraphicsPipeline(m_pass.entityPipeline.get());
        commandBuffer->setShaderResources();
        drawBuffers(commandBuffer, m_entityBuffers);
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
    const QVector3D horizon = m_sky.horizon;
    const bool ready = m_pass.opaquePipeline && m_pass.translucentPipeline && m_pass.waterPipeline;
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
    QElapsedTimer renderTime;
    renderTime.start();
    startSample(commandBuffer);
    QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
    prepareResources(updates);

    const QSize outputSize = renderTarget()->pixelSize();
    const float aspect = outputSize.height() > 0 ? float(outputSize.width()) / float(outputSize.height()) : 1.0f;
    const float farPlane = m_fogEnd + QcCamera::farPlaneMargin;
    const QMatrix4x4 viewProjection =
        m_rhi->clipSpaceCorrMatrix() * m_camera.projectionMatrix(aspect, farPlane) * m_camera.viewMatrix();
    uploadFrameUniforms(updates, viewProjection, outputSize);
    m_pass.debugLineRenderer.prepare(m_rhi, renderTarget(), updates, m_frameUniforms.get(), m_debugLines);
    m_pass.precipitationRenderer.prepare(m_rhi, renderTarget(), updates, m_frameUniforms.get(), m_precipitation);
    // The sky is seen from the camera's position: the same view without the translation.
    QMatrix4x4 viewRotation = m_camera.viewMatrix();
    viewRotation.setColumn(3, QVector4D(0.0f, 0.0f, 0.0f, 1.0f));
    m_pass.skyRenderer.prepare(m_rhi, renderTarget(), updates,
                          {m_rhi->clipSpaceCorrMatrix() * m_camera.projectionMatrix(aspect, farPlane) * viewRotation, m_sky, m_camera.position});

    const FrameView view = cull(viewProjection);
    m_sample.visibleSections = int(view.visible.size());
    drawPlan(commandBuffer, updates, view, outputSize);

    if (m_collectSamples) {
        m_sample.sections = int(m_meshStore.meshes().size());
        m_sample.sectionBytes = m_meshStore.bytes();
        m_sample.worldRenderMs = double(renderTime.nsecsElapsed()) / 1e6;
        m_samples.append(m_sample);
    }
}
