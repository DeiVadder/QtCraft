#include "QcWorldRenderer.h"

#include "QcWorldView.h"

#include <QFile>
#include <QLoggingCategory>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcRenderer, "qtcraft.renderer")

// std140 layout of the Frame uniform block in block.vert/block.frag.
struct FrameUniforms
{
    float viewProjection[16];
    float cameraPosition[4];
    float fogColor[4];
    float params[4];
};
static_assert(sizeof(FrameUniforms) == 112);

constexpr float minimumLight = 0.08f;

QShader loadShader(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCCritical(lcRenderer) << "Cannot open shader" << path;
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

} // namespace

void QcWorldRenderer::initialize(QRhiCommandBuffer *commandBuffer)
{
    Q_UNUSED(commandBuffer);
    if (m_rhi != rhi()) {
        m_meshes.clear();
        m_opaquePipeline.reset();
        m_translucentPipeline.reset();
        m_outlinePipeline.reset();
        m_outlineBindings.reset();
        m_outlineVertices.reset();
        m_uploadedSelection.reset();
        m_entityVertices.reset();
        m_entityIndices.reset();
        m_bindings.reset();
        m_atlasTexture.reset();
        m_atlasSampler.reset();
        m_frameUniforms.reset();
        m_pipelineRenderPass.reset();
        m_uploadedAtlasVersion = 0;
        m_rhi = rhi();
    }

    if (!m_frameUniforms) {
        m_frameUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(FrameUniforms)));
        m_frameUniforms->create();
        m_atlasSampler.reset(m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                               QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        m_atlasSampler->create();
    }

    if (!m_pipelineRenderPass || !m_pipelineRenderPass->isCompatible(renderTarget()->renderPassDescriptor())) {
        m_opaquePipeline.reset();
        m_translucentPipeline.reset();
        m_outlinePipeline.reset();
    }
}

void QcWorldRenderer::synchronize(QQuickRhiItem *item)
{
    auto *view = static_cast<QcWorldView *>(item);
    m_camera = view->m_camera;
    m_selection = view->m_selection;
    m_entityMesh = view->m_entityMesh;
    m_skyColor = view->m_skyColor;
    m_daylight = view->m_daylight;
    m_fogStart = view->m_fogStart;
    m_fogEnd = view->m_fogEnd;
    if (view->m_atlasVersion != m_atlasVersion) {
        m_atlasVersion = view->m_atlasVersion;
        m_atlasImage = view->m_atlas;
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
}

void QcWorldRenderer::ensureAtlasTexture(QRhiResourceUpdateBatch *updates)
{
    if (m_atlasTexture && m_uploadedAtlasVersion == m_atlasVersion) {
        return;
    }
    QImage image = m_atlasImage.isNull() ? QImage(16, 16, QImage::Format_RGBA8888) : m_atlasImage;
    if (m_atlasImage.isNull()) {
        image.fill(Qt::magenta);
    }
    image = image.convertToFormat(QImage::Format_RGBA8888);

    if (!m_atlasTexture || m_atlasTexture->pixelSize() != image.size()) {
        m_atlasTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, image.size()));
        m_atlasTexture->create();
        m_bindings.reset();
        m_opaquePipeline.reset();
        m_translucentPipeline.reset();
    }
    updates->uploadTexture(m_atlasTexture.get(), image);
    m_uploadedAtlasVersion = m_atlasVersion;
}

void QcWorldRenderer::createPipelines()
{
    if (!m_bindings) {
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

    const QShader vertexShader = loadShader(u":/shaders/block.vert.qsb"_s);
    const QShader fragmentShader = loadShader(u":/shaders/block.frag.qsb"_s);

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{sizeof(QcBlockVertex)}});
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3, quint32(offsetof(QcBlockVertex, x))},
        {0, 1, QRhiVertexInputAttribute::Float2, quint32(offsetof(QcBlockVertex, u))},
        {0, 2, QRhiVertexInputAttribute::Float, quint32(offsetof(QcBlockVertex, shade))},
        {0, 3, QRhiVertexInputAttribute::Float, quint32(offsetof(QcBlockVertex, skyLight))},
    });

    const auto makePipeline = [&](bool translucent) {
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
    };

    m_opaquePipeline = makePipeline(false);
    m_translucentPipeline = makePipeline(true);
    m_pipelineRenderPass.reset(renderTarget()->renderPassDescriptor()->newCompatibleRenderPassDescriptor());
}

void QcWorldRenderer::createOutlinePipeline()
{
    if (!m_outlineBindings) {
        m_outlineBindings.reset(m_rhi->newShaderResourceBindings());
        m_outlineBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_frameUniforms.get())});
        m_outlineBindings->create();
        // 12 box edges as line segments.
        m_outlineVertices.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, quint32(std::size_t(24) * 3 * sizeof(float))));
        m_outlineVertices->create();
        m_uploadedSelection.reset();
    }
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(m_rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, loadShader(u":/shaders/outline.vert.qsb"_s)},
                               {QRhiShaderStage::Fragment, loadShader(u":/shaders/outline.frag.qsb"_s)}});
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
    if (!m_selection || m_selection == m_uploadedSelection || !m_outlineVertices) {
        return;
    }
    // Slightly larger than the block so the lines are not hidden by its faces.
    constexpr float grow = 0.004f;
    const float x0 = float(m_selection->x) - grow;
    const float y0 = float(m_selection->y) - grow;
    const float z0 = float(m_selection->z) - grow;
    const float x1 = float(m_selection->x) + 1.0f + grow;
    const float y1 = float(m_selection->y) + 1.0f + grow;
    const float z1 = float(m_selection->z) + 1.0f + grow;
    const std::array<std::array<float, 3>, 8> corners = {{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1},
                                                          {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}};
    constexpr std::array<std::pair<int, int>, 12> edges = {
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
    std::array<float, std::size_t(24) * 3> vertices{};
    std::size_t next = 0;
    for (const auto &[from, to] : edges) {
        for (const int corner : {from, to}) {
            for (const float value : corners[std::size_t(corner)]) {
                vertices[next++] = value;
            }
        }
    }
    updates->updateDynamicBuffer(m_outlineVertices.get(), 0, quint32(sizeof(vertices)), vertices.data());
    m_uploadedSelection = m_selection;
}

void QcWorldRenderer::uploadEntities(QRhiResourceUpdateBatch *updates)
{
    m_entityIndexCount = 0;
    if (!m_entityMesh || m_entityMesh->opaqueIndices.isEmpty()) {
        return;
    }
    const auto vertexBytes = quint32(m_entityMesh->opaqueVertices.size() * qsizetype(sizeof(QcBlockVertex)));
    const auto indexBytes = quint32(m_entityMesh->opaqueIndices.size() * qsizetype(sizeof(quint32)));
    // Dynamic buffers grow on demand and are reused every frame.
    if (!m_entityVertices || m_entityVertices->size() < vertexBytes) {
        m_entityVertices.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, vertexBytes * 2));
        m_entityVertices->create();
    }
    if (!m_entityIndices || m_entityIndices->size() < indexBytes) {
        m_entityIndices.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, indexBytes * 2));
        m_entityIndices->create();
    }
    updates->updateDynamicBuffer(m_entityVertices.get(), 0, vertexBytes, m_entityMesh->opaqueVertices.constData());
    updates->updateDynamicBuffer(m_entityIndices.get(), 0, indexBytes, m_entityMesh->opaqueIndices.constData());
    m_entityIndexCount = quint32(m_entityMesh->opaqueIndices.size());
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
    const quint32 indexBytes = quint32(indices.size() * qsizetype(sizeof(quint32)));
    buffers.vertices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vertexBytes));
    buffers.indices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, indexBytes));
    buffers.vertices->create();
    buffers.indices->create();
    updates->uploadStaticBuffer(buffers.vertices.get(), vertices.constData());
    updates->uploadStaticBuffer(buffers.indices.get(), indices.constData());
    buffers.indexCount = quint32(indices.size());
    return buffers;
}

void QcWorldRenderer::uploadPendingMeshes(QRhiResourceUpdateBatch *updates)
{
    if (m_clearAllMeshes) {
        m_meshes.clear();
        m_clearAllMeshes = false;
    }
    for (auto &[key, mesh] : m_meshUploads) {
        if (!mesh) {
            m_meshes.erase(key);
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
        m_meshes.insert_or_assign(key, std::move(gpuMesh));
    }
    m_meshUploads.clear();
}

bool QcWorldRenderer::isVisible(const GpuMesh &mesh, const QMatrix4x4 &viewProjection) const
{
    // A box is culled when all eight corners lie outside the same clip plane.
    std::array<int, 6> outside{};
    for (int corner = 0; corner < 8; ++corner) {
        const QVector4D point((corner & 1) ? mesh.boundsMax.x() : mesh.boundsMin.x(),
                              (corner & 2) ? mesh.boundsMax.y() : mesh.boundsMin.y(),
                              (corner & 4) ? mesh.boundsMax.z() : mesh.boundsMin.z(), 1.0f);
        const QVector4D clip = viewProjection * point;
        outside[0] += clip.x() < -clip.w();
        outside[1] += clip.x() > clip.w();
        outside[2] += clip.y() < -clip.w();
        outside[3] += clip.y() > clip.w();
        outside[4] += clip.z() > clip.w();
        outside[5] += clip.w() <= 0.0f;
    }
    return std::ranges::none_of(outside, [](int count) { return count == 8; });
}

void QcWorldRenderer::render(QRhiCommandBuffer *commandBuffer)
{
    QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
    ensureAtlasTexture(updates);
    if (!m_opaquePipeline || !m_translucentPipeline) {
        createPipelines();
    }
    if (!m_outlinePipeline) {
        createOutlinePipeline();
    }
    uploadOutline(updates);
    uploadEntities(updates);
    uploadPendingMeshes(updates);

    const QSize outputSize = renderTarget()->pixelSize();
    const float aspect = outputSize.height() > 0 ? float(outputSize.width()) / float(outputSize.height()) : 1.0f;
    const float farPlane = m_fogEnd + QcCamera::farPlaneMargin;
    const QMatrix4x4 viewProjection =
        m_rhi->clipSpaceCorrMatrix() * m_camera.projectionMatrix(aspect, farPlane) * m_camera.viewMatrix();

    FrameUniforms uniforms{};
    std::copy_n(viewProjection.constData(), 16, uniforms.viewProjection);
    uniforms.cameraPosition[0] = m_camera.position.x();
    uniforms.cameraPosition[1] = m_camera.position.y();
    uniforms.cameraPosition[2] = m_camera.position.z();
    uniforms.fogColor[0] = m_skyColor.redF();
    uniforms.fogColor[1] = m_skyColor.greenF();
    uniforms.fogColor[2] = m_skyColor.blueF();
    uniforms.fogColor[3] = 1.0f;
    uniforms.params[0] = m_fogStart;
    uniforms.params[1] = m_fogEnd;
    uniforms.params[2] = m_daylight;
    uniforms.params[3] = minimumLight;
    updates->updateDynamicBuffer(m_frameUniforms.get(), 0, sizeof(FrameUniforms), &uniforms);

    commandBuffer->beginPass(renderTarget(), m_skyColor, {1.0f, 0}, updates);
    if (m_opaquePipeline && m_translucentPipeline) {
        commandBuffer->setViewport(QRhiViewport(0, 0, float(outputSize.width()), float(outputSize.height())));

        QList<const GpuMesh *> visible;
        visible.reserve(m_meshes.size());
        for (const auto &[key, mesh] : m_meshes) {
            if (isVisible(mesh, viewProjection)) {
                visible.append(&mesh);
            }
        }

        const auto drawBuffers = [commandBuffer](const GpuBuffers &buffers) {
            if (buffers.indexCount == 0) {
                return;
            }
            const QRhiCommandBuffer::VertexInput input(buffers.vertices.get(), 0);
            commandBuffer->setVertexInput(0, 1, &input, buffers.indices.get(), 0, QRhiCommandBuffer::IndexUInt32);
            commandBuffer->drawIndexed(buffers.indexCount);
        };

        commandBuffer->setGraphicsPipeline(m_opaquePipeline.get());
        commandBuffer->setShaderResources();
        for (const GpuMesh *mesh : std::as_const(visible)) {
            drawBuffers(mesh->opaque);
        }
        if (m_entityIndexCount > 0) {
            const QRhiCommandBuffer::VertexInput input(m_entityVertices.get(), 0);
            commandBuffer->setVertexInput(0, 1, &input, m_entityIndices.get(), 0, QRhiCommandBuffer::IndexUInt32);
            commandBuffer->drawIndexed(m_entityIndexCount);
        }

        // Back-to-front by section centre so overlapping translucent sections blend correctly.
        const QVector3D eye = m_camera.position;
        std::ranges::sort(visible, [&eye](const GpuMesh *a, const GpuMesh *b) {
            const QVector3D centreA = (a->boundsMin + a->boundsMax) * 0.5f;
            const QVector3D centreB = (b->boundsMin + b->boundsMax) * 0.5f;
            return (centreA - eye).lengthSquared() > (centreB - eye).lengthSquared();
        });
        commandBuffer->setGraphicsPipeline(m_translucentPipeline.get());
        commandBuffer->setShaderResources();
        for (const GpuMesh *mesh : std::as_const(visible)) {
            drawBuffers(mesh->translucent);
        }

        if (m_selection && m_outlinePipeline) {
            commandBuffer->setGraphicsPipeline(m_outlinePipeline.get());
            commandBuffer->setShaderResources();
            const QRhiCommandBuffer::VertexInput input(m_outlineVertices.get(), 0);
            commandBuffer->setVertexInput(0, 1, &input);
            commandBuffer->draw(24);
        }
    }
    commandBuffer->endPass();
}
