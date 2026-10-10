#include "QcCharacterPreviewRenderer.h"

#include "QcCharacterPreview.h"
#include "QcPreviewAtlasCache.h"
#include "QcRenderPipelines.h"

#include <QLoggingCategory>

#include <algorithm>
#include <atomic>

using namespace Qt::StringLiterals;

namespace {

// Far enough that no fog reaches the figure.
constexpr float noFog = 1.0e4f;
constexpr float farPlane = 32.0f;

Q_LOGGING_CATEGORY(lcPreview, "qtcraft.preview")

std::atomic<int> liveRenderers = 0;

} // namespace

QcCharacterPreviewRenderer::QcCharacterPreviewRenderer()
{
    ++liveRenderers;
}

QcCharacterPreviewRenderer::~QcCharacterPreviewRenderer()
{
    releaseResources();
    qCInfo(lcPreview) << "Character preview renderer released:" << --liveRenderers << "renderers," << QcPreviewAtlasCache::liveTextures()
                      << "atlas textures";
}

void QcCharacterPreviewRenderer::releaseResources()
{
    m_pipeline.reset();
    m_pipelineRenderPass.reset();
    m_bindings.reset();
    m_atlasTexture.reset();
    m_sampler.reset();
    m_uniforms.reset();
    m_vertices.reset();
    m_indices.reset();
    m_uploadedMesh.reset();
    m_indexCount = 0;
    m_uploadedAtlasVersion = 0;
    m_pipelineFailed = false;
}

void QcCharacterPreviewRenderer::initialize(QRhiCommandBuffer *commandBuffer)
{
    Q_UNUSED(commandBuffer);
    if (m_rhi != rhi()) {
        releaseResources();
        m_rhi = rhi();
    }
    if (m_pipelineRenderPass && !m_pipelineRenderPass->isCompatible(renderTarget()->renderPassDescriptor())) {
        m_pipeline.reset();
        m_pipelineFailed = false;
    }

    if (!m_uniforms) {
        m_uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(QcRenderPipelines::FrameUniforms)));
        m_uniforms->create();
        m_sampler.reset(m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                          QRhiSampler::ClampToEdge));
        m_sampler->create();
    }
}

void QcCharacterPreviewRenderer::synchronize(QQuickRhiItem *item)
{
    const auto *preview = static_cast<QcCharacterPreview *>(item);
    m_mesh = preview->m_mesh;
    m_camera = preview->m_viewCamera;
    if (preview->m_atlasVersion != m_atlasVersion) {
        m_atlasVersion = preview->m_atlasVersion;
        m_atlasImage = preview->m_atlas;
    }
}

void QcCharacterPreviewRenderer::prepare(QRhiResourceUpdateBatch *updates)
{
    if (!m_atlasTexture || m_uploadedAtlasVersion != m_atlasVersion) {
        std::shared_ptr<QRhiTexture> texture = QcPreviewAtlasCache::texture(m_rhi, m_atlasImage, updates);
        if (texture != m_atlasTexture) {
            const bool first = !m_atlasTexture;
            m_atlasTexture = std::move(texture);
            if (first) {
                qCInfo(lcPreview) << "Character preview renderer ready:" << liveRenderers.load() << "renderers," << QcPreviewAtlasCache::liveTextures()
                                  << "atlas textures";
            }
            m_bindings.reset();
            m_pipeline.reset();
            m_pipelineFailed = false;
        }
        m_uploadedAtlasVersion = m_atlasVersion;
    }
    if (!m_atlasTexture) {
        return;
    }

    if (!m_bindings) {
        m_bindings.reset(m_rhi->newShaderResourceBindings());
        m_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                                                     m_uniforms.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_atlasTexture.get(), m_sampler.get()),
        });
        m_bindings->create();
    }
    if (!m_pipeline && !m_pipelineFailed) {
        m_pipeline = QcRenderPipelines::createEntityPipeline(m_rhi, renderTarget(), m_bindings.get(),
                                                             QcRenderPipelines::loadShader(u":/shaders/entity.vert.qsb"_s, lcPreview()),
                                                             QcRenderPipelines::loadShader(u":/shaders/entity.frag.qsb"_s, lcPreview()));
        m_pipelineRenderPass.reset(renderTarget()->renderPassDescriptor()->newCompatibleRenderPassDescriptor());
        m_pipelineFailed = !m_pipeline;
    }
}

void QcCharacterPreviewRenderer::uploadMesh(QRhiResourceUpdateBatch *updates)
{
    if (m_mesh == m_uploadedMesh) {
        return;
    }
    m_uploadedMesh = m_mesh;
    m_indexCount = 0;
    if (!m_mesh || m_mesh->indices.isEmpty()) {
        return;
    }

    const auto vertexBytes = quint32(m_mesh->vertices.size() * qsizetype(sizeof(QcEntityVertex)));
    const auto indexBytes = quint32(m_mesh->indices.size() * qsizetype(sizeof(quint32)));
    // Buffers grow on demand and are reused for every frame.
    if (!m_vertices || m_vertices->size() < vertexBytes) {
        m_vertices.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, vertexBytes));
        m_vertices->create();
    }
    if (!m_indices || m_indices->size() < indexBytes) {
        m_indices.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, indexBytes));
        m_indices->create();
    }
    updates->updateDynamicBuffer(m_vertices.get(), 0, vertexBytes, m_mesh->vertices.constData());
    updates->updateDynamicBuffer(m_indices.get(), 0, indexBytes, m_mesh->indices.constData());
    m_indexCount = quint32(m_mesh->indices.size());
}

void QcCharacterPreviewRenderer::uploadUniforms(QRhiResourceUpdateBatch *updates)
{
    const QSize size = renderTarget()->pixelSize();
    const float aspect = size.height() > 0 ? float(size.width()) / float(size.height()) : 1.0f;
    const QMatrix4x4 viewProjection = m_rhi->clipSpaceCorrMatrix() * m_camera.projectionMatrix(aspect, farPlane) * m_camera.viewMatrix();

    QcRenderPipelines::FrameUniforms uniforms{};
    std::copy_n(viewProjection.constData(), 16, uniforms.viewProjection);
    uniforms.cameraPosition[0] = m_camera.position.x();
    uniforms.cameraPosition[1] = m_camera.position.y();
    uniforms.cameraPosition[2] = m_camera.position.z();
    uniforms.params[0] = noFog;
    uniforms.params[1] = noFog * 2.0f;
    // Full daylight: as at noon in the open.
    uniforms.params[2] = 1.0f;
    uniforms.params[3] = QcRenderPipelines::minimumLight;
    uniforms.extra[0] = m_rhi->isClipDepthZeroToOne() ? 1.0f : 0.0f;
    uniforms.extra[1] = float(size.width());
    uniforms.extra[2] = float(size.height());
    updates->updateDynamicBuffer(m_uniforms.get(), 0, sizeof(uniforms), &uniforms);
}

void QcCharacterPreviewRenderer::render(QRhiCommandBuffer *commandBuffer)
{
    QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
    if (!m_atlasImage.isNull()) {
        prepare(updates);
        uploadMesh(updates);
        uploadUniforms(updates);
    }

    commandBuffer->beginPass(renderTarget(), Qt::transparent, {1.0f, 0}, updates);
    if (m_pipeline && m_indexCount > 0) {
        const QSize size = renderTarget()->pixelSize();
        commandBuffer->setGraphicsPipeline(m_pipeline.get());
        commandBuffer->setViewport(QRhiViewport(0, 0, float(size.width()), float(size.height())));
        commandBuffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput input(m_vertices.get(), 0);
        commandBuffer->setVertexInput(0, 1, &input, m_indices.get(), 0, QRhiCommandBuffer::IndexUInt32);
        commandBuffer->drawIndexed(m_indexCount);
    }
    commandBuffer->endPass();
}
