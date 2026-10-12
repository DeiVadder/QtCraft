#include "QcWorldRenderer.h"

#include "QcAtlasTexture.h"

#include "QcFrustum.h"
#include "QcIndexPacking.h"
#include "QcRenderPipelines.h"
#include "QcWorldView.h"
#include "QcThreadCpuClock.h"

#include <QElapsedTimer>
#include <QLoggingCategory>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

using namespace Qt::StringLiterals;
using QcRenderPipelines::FrameUniforms;
using QcRenderPipelines::loadShader;
using QcRenderPipelines::minimumLight;

namespace {

Q_LOGGING_CATEGORY(lcRenderer, "qtcraft.renderer")

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

void QcWorldRenderer::initialize(QRhiCommandBuffer *commandBuffer)
{
    Q_UNUSED(commandBuffer);
    if (m_rhi != rhi()) {
        m_meshes.clear();
        m_sectionBytes = 0;
        m_opaquePipeline.reset();
        m_translucentPipeline.reset();
        m_entityPipeline.reset();
        m_handPipeline.reset();
        m_outlinePipeline.reset();
        m_outlineBindings.reset();
        m_outlineVertices.reset();
        m_uploadedSelection.reset();
        m_entityBuffers = {};
        m_handBuffers = {};
        m_bindings.reset();
        m_atlasTexture.reset();
        m_atlasSampler.reset();
        m_frameUniforms.reset();
        m_pipelineRenderPass.reset();
        m_skyRenderer.reset();
        m_debugLineRenderer.reset();
        m_uploadedAtlasVersion = 0;
        m_atlasTried = false;
        m_rhi = rhi();
    }

    if (!m_frameUniforms) {
        m_frameUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(FrameUniforms)));
        m_frameUniforms->create();
        m_atlasSampler = QcAtlasTexture::createSampler(m_rhi);
    }

    if (!m_pipelineRenderPass || !m_pipelineRenderPass->isCompatible(renderTarget()->renderPassDescriptor())) {
        m_opaquePipeline.reset();
        m_translucentPipeline.reset();
        m_entityPipeline.reset();
        m_handPipeline.reset();
        m_outlinePipeline.reset();
        m_skyRenderer.reset();
        m_debugLineRenderer.reset();
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
    m_sky = view->m_sky;
    m_fogStart = view->m_fogStart;
    m_fogEnd = view->m_fogEnd;

    if (view->m_atlasVersion != m_atlasVersion) {
        m_atlasVersion = view->m_atlasVersion;
        m_atlasLevels = view->m_atlas;
    }
    if (view->m_clearAllMeshes) {
        view->m_clearAllMeshes = false;
        m_clearAllMeshes = true;
        m_meshUploads.clear();
    }

    for (QcWorldView::MeshUpdate &update : view->m_pendingMeshUpdates) {
        m_meshUploads.append({update.key, std::move(update.mesh)});
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
    m_opaquePipeline.reset();
    m_translucentPipeline.reset();
    m_entityPipeline.reset();
    m_handPipeline.reset();
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
    m_opaquePipeline = createBlockPipeline(vertexShader, fragmentShader, inputLayout, false);
    m_translucentPipeline = createBlockPipeline(vertexShader, fragmentShader, inputLayout, true);

    const QShader entityFragment = loadShader(u":/shaders/entity.frag.qsb"_s, lcRenderer());
    m_entityPipeline = QcRenderPipelines::createEntityPipeline(m_rhi, renderTarget(), m_bindings.get(),
                                                               loadShader(u":/shaders/entity.vert.qsb"_s, lcRenderer()), entityFragment);
    m_handPipeline = QcRenderPipelines::createEntityPipeline(m_rhi, renderTarget(), m_bindings.get(), loadShader(u":/shaders/hand.vert.qsb"_s, lcRenderer()),
                                                             entityFragment);
    m_pipelineRenderPass.reset(renderTarget()->renderPassDescriptor()->newCompatibleRenderPassDescriptor());
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
        m_outlinePipeline = std::move(pipeline);
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

void QcWorldRenderer::uploadEntityMesh(QRhiResourceUpdateBatch *updates, const QcEntityMesh *mesh, GpuBuffers &buffers)
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

QcWorldRenderer::GpuBuffers QcWorldRenderer::createBuffers(QRhiResourceUpdateBatch *updates,
                                                           const QList<QcBlockVertex> &vertices,
                                                           const QList<quint32> &indices)
{
    GpuBuffers buffers;
    if (indices.isEmpty()) {
        return buffers;
    }

    const quint32 vertexBytes = quint32(vertices.size() * qsizetype(sizeof(QcBlockVertex)));
    buffers.vertices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vertexBytes));
    buffers.vertices->create();
    updates->uploadStaticBuffer(buffers.vertices.get(), vertices.constData());

    // Almost every section has at most 65536 vertices; its indices then take two bytes each.
    if (QcIndexPacking::fitsShortIndices(vertices.size())) {
        const QByteArray shortIndices = QcIndexPacking::toShortIndices(indices);
        buffers.indices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, quint32(shortIndices.size())));
        buffers.indices->create();
        updates->uploadStaticBuffer(buffers.indices.get(), shortIndices.constData());
        buffers.indexFormat = QRhiCommandBuffer::IndexUInt16;
    } else {
        const quint32 indexBytes = quint32(indices.size() * qsizetype(sizeof(quint32)));
        buffers.indices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, indexBytes));
        buffers.indices->create();
        updates->uploadStaticBuffer(buffers.indices.get(), indices.constData());
    }

    buffers.indexCount = quint32(indices.size());
    return buffers;
}

void QcWorldRenderer::uploadPendingMeshes(QRhiResourceUpdateBatch *updates)
{
    if (m_clearAllMeshes) {
        m_meshes.clear();
        m_sectionBytes = 0;
        m_clearAllMeshes = false;
    }

    const auto release = [this](quint64 key) {
        if (const auto found = m_meshes.find(key); found != m_meshes.end()) {
            m_sectionBytes -= found->second.opaque.bytes() + found->second.translucent.bytes();
            m_meshes.erase(found);
        }
    };

    for (auto &[key, mesh] : m_meshUploads) {
        release(key);
        if (!mesh) {
            continue;
        }

        GpuMesh gpuMesh;
        gpuMesh.opaque = createBuffers(updates, mesh->opaqueVertices, mesh->opaqueIndices);
        gpuMesh.translucent = createBuffers(updates, mesh->translucentVertices, mesh->translucentIndices);

        QVector3D low(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max());
        QVector3D high = -low;
        for (const QList<QcBlockVertex> *list : {&mesh->opaqueVertices, &mesh->translucentVertices}) {
            for (const QcBlockVertex &vertex : *list) {
                low = QVector3D(std::min(low.x(), vertex.x), std::min(low.y(), vertex.y), std::min(low.z(), vertex.z));
                high = QVector3D(std::max(high.x(), vertex.x), std::max(high.y(), vertex.y), std::max(high.z(), vertex.z));
            }
        }

        gpuMesh.boundsMin = low;
        gpuMesh.boundsMax = high;
        const quint64 bytes = gpuMesh.opaque.bytes() + gpuMesh.translucent.bytes();
        m_sectionBytes += bytes;
        m_sample.uploadBytes += bytes;
        m_meshes.insert_or_assign(key, std::move(gpuMesh));
    }

    m_meshUploads.clear();
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
    if (m_atlasTexture && (!m_opaquePipeline || !m_translucentPipeline)) {
        createPipelines();
    }
    if (!m_outlinePipeline) {
        createOutlinePipeline();
    }

    QElapsedTimer uploadTime;
    uploadTime.start();
    uploadOutline(updates);
    uploadEntityMesh(updates, m_entityMesh.get(), m_entityBuffers);
    uploadEntityMesh(updates, m_handMesh.get(), m_handBuffers);
    uploadPendingMeshes(updates);
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
    uniforms.params[0] = m_fogStart;
    uniforms.params[1] = m_fogEnd;
    uniforms.params[2] = float(m_sky.daylight);
    uniforms.params[3] = minimumLight;
    uniforms.extra[0] = m_rhi->isClipDepthZeroToOne() ? 1.0f : 0.0f;
    uniforms.extra[1] = float(outputSize.width());
    uniforms.extra[2] = float(outputSize.height());

    updates->updateDynamicBuffer(m_frameUniforms.get(), 0, sizeof(FrameUniforms), &uniforms);
}

void QcWorldRenderer::drawBuffers(QRhiCommandBuffer *commandBuffer, const GpuBuffers &buffers)
{
    if (buffers.indexCount == 0) {
        return;
    }

    const QRhiCommandBuffer::VertexInput input(buffers.vertices.get(), 0);
    commandBuffer->setVertexInput(0, 1, &input, buffers.indices.get(), 0, buffers.indexFormat);
    commandBuffer->drawIndexed(buffers.indexCount);
    ++m_sample.drawCalls;
    m_sample.indicesDrawn += buffers.indexCount;
}

void QcWorldRenderer::drawTranslucent(QRhiCommandBuffer *commandBuffer, const QList<const GpuMesh *> &visible)
{
    // Back-to-front by section centre so overlapping translucent sections blend correctly; only
    // the sections with translucent faces take part.
    const QVector3D eye = m_camera.position;
    QList<std::pair<float, const GpuMesh *>> translucent;
    for (const GpuMesh *mesh : visible) {
        if (mesh->translucent.indexCount > 0) {
            translucent.append({((mesh->boundsMin + mesh->boundsMax) * 0.5f - eye).lengthSquared(), mesh});
        }
    }
    std::ranges::sort(translucent, std::ranges::greater{}, &std::pair<float, const GpuMesh *>::first);

    commandBuffer->setGraphicsPipeline(m_translucentPipeline.get());
    commandBuffer->setShaderResources();
    for (const auto &[distance, mesh] : std::as_const(translucent)) {
        drawBuffers(commandBuffer, mesh->translucent);
    }
}

void QcWorldRenderer::drawOutlineAndHand(QRhiCommandBuffer *commandBuffer)
{
    if (!m_selection.isEmpty() && m_outlinePipeline && m_outlineVertexCount > 0) {
        commandBuffer->setGraphicsPipeline(m_outlinePipeline.get());
        commandBuffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput input(m_outlineVertices.get(), 0);
        commandBuffer->setVertexInput(0, 1, &input);
        commandBuffer->draw(m_outlineVertexCount);
        ++m_sample.drawCalls;
    }
    // Last, with its depth squeezed to the front: the arm never disappears into a wall.
    if (m_handBuffers.indexCount > 0 && m_handPipeline) {
        commandBuffer->setGraphicsPipeline(m_handPipeline.get());
        commandBuffer->setShaderResources();
        drawBuffers(commandBuffer, m_handBuffers);
    }
}

void QcWorldRenderer::drawWorld(QRhiCommandBuffer *commandBuffer, const QMatrix4x4 &viewProjection, QSize outputSize)
{
    commandBuffer->setViewport(QRhiViewport(0, 0, float(outputSize.width()), float(outputSize.height())));
    m_skyRenderer.draw(commandBuffer);

    const QcFrustum::Planes planes = QcFrustum::planes(viewProjection);
    QList<const GpuMesh *> visible;
    visible.reserve(m_meshes.size());
    for (const auto &[key, mesh] : m_meshes) {
        if (QcFrustum::intersectsBox(planes, mesh.boundsMin, mesh.boundsMax)) {
            visible.append(&mesh);
        }
    }
    m_sample.visibleSections = int(visible.size());

    commandBuffer->setGraphicsPipeline(m_opaquePipeline.get());
    commandBuffer->setShaderResources();
    for (const GpuMesh *mesh : std::as_const(visible)) {
        drawBuffers(commandBuffer, mesh->opaque);
    }
    if (m_entityBuffers.indexCount > 0 && m_entityPipeline) {
        commandBuffer->setGraphicsPipeline(m_entityPipeline.get());
        commandBuffer->setShaderResources();
        drawBuffers(commandBuffer, m_entityBuffers);
    }

    drawTranslucent(commandBuffer, visible);
    m_sample.drawCalls += m_debugLineRenderer.draw(commandBuffer) ? 1 : 0;
    drawOutlineAndHand(commandBuffer);
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
    m_debugLineRenderer.prepare(m_rhi, renderTarget(), updates, m_frameUniforms.get(), m_debugLines);
    // The sky is seen from the camera's position: the same view without the translation.
    QMatrix4x4 viewRotation = m_camera.viewMatrix();
    viewRotation.setColumn(3, QVector4D(0.0f, 0.0f, 0.0f, 1.0f));
    m_skyRenderer.prepare(m_rhi, renderTarget(), updates,
                          {m_rhi->clipSpaceCorrMatrix() * m_camera.projectionMatrix(aspect, farPlane) * viewRotation, m_sky, m_camera.position});

    const QVector3D horizon = m_sky.horizon;
    commandBuffer->beginPass(renderTarget(), QColor::fromRgbF(horizon.x(), horizon.y(), horizon.z()), {1.0f, 0}, updates);
    if (m_opaquePipeline && m_translucentPipeline) {
        drawWorld(commandBuffer, viewProjection, outputSize);
    }
    commandBuffer->endPass();

    if (m_collectSamples) {
        m_sample.sections = int(m_meshes.size());
        m_sample.sectionBytes = m_sectionBytes;
        m_sample.worldRenderMs = double(renderTime.nsecsElapsed()) / 1e6;
        m_samples.append(m_sample);
    }
}
