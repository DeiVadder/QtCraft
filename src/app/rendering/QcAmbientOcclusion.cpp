#include "app/rendering/QcAmbientOcclusion.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcRenderPipelines.h"

#include <algorithm>

using namespace Qt::StringLiterals;
using QcRenderPipelines::loadShader;

namespace {

// std140 layout of the Occlusion block of occlusion.frag.
struct OcclusionUniforms
{
    float viewProjection[16];
    float inverseViewProjection[16];
    // x: radius, y: strength, z: samples, w: 1 for clip depth 0..1.
    float params[4];
    // x, y: fade start and end, z: nearest distance, w: 1 when texture coordinates grow with clip y, else -1.
    float view[4];
    // xy: one texel of the depth texture, zw: one pixel of the target, in texture coordinates.
    float texel[4];
};
static_assert(sizeof(OcclusionUniforms) == 176);

// Half of `size`, at least one pixel.
QSize halved(QSize size)
{
    return {std::max(size.width() / 2, 1), std::max(size.height() / 2, 1)};
}

} // namespace

bool QcAmbientOcclusion::prepare(QRhi *rhi, QRhiTexture *depth, int samples)
{
    m_samples = samples;
    // A pass the GPU refused is not tried again every frame.
    const Key key{depth, depth->pixelSize()};
    if (m_built == key) {
        return result() != nullptr;
    }

    releaseResources();
    m_built = key;
    m_depthZeroToOne = rhi->isClipDepthZeroToOne();
    m_uvMatchesNdcY = rhi->isYUpInFramebuffer() == rhi->isYUpInNDC();
    if (create(rhi, depth)) {
        ++m_generation;
        return true;
    }

    qCWarning(lcRenderer) << "Ambient occlusion: the GPU refused a part of the pass; the realistic mode draws without it";
    releaseResources();
    return false;
}

bool QcAmbientOcclusion::create(QRhi *rhi, QRhiTexture *depth)
{
    m_uniforms.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(OcclusionUniforms)));
    m_sampler.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                    QRhiSampler::ClampToEdge));
    if (!m_uniforms->create() || !m_sampler->create() || !createTarget(rhi, halved(depth->pixelSize()))) {
        return false;
    }

    constexpr auto stage = QRhiShaderResourceBinding::FragmentStage;
    m_bindings.reset(rhi->newShaderResourceBindings());
    m_bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, stage, m_uniforms.get()),
                             QRhiShaderResourceBinding::sampledTexture(1, stage, depth, m_sampler.get())});
    if (!m_bindings->create()) {
        return false;
    }

    m_pipeline.reset(rhi->newGraphicsPipeline());
    m_pipeline->setShaderStages({{QRhiShaderStage::Vertex, loadShader(u":/shaders/post.vert.qsb"_s, lcRenderer())},
                                 {QRhiShaderStage::Fragment, loadShader(u":/shaders/occlusion.frag.qsb"_s, lcRenderer())}});
    // No vertex buffer: the shader makes the triangle from the vertex index.
    m_pipeline->setVertexInputLayout({});
    m_pipeline->setShaderResourceBindings(m_bindings.get());
    m_pipeline->setRenderPassDescriptor(m_renderPass.get());
    if (!m_pipeline->create()) {
        m_pipeline.reset();
        return false;
    }
    return true;
}

bool QcAmbientOcclusion::createTarget(QRhi *rhi, QSize size)
{
    for (const QRhiTexture::Format format : {QRhiTexture::R8, QRhiTexture::RGBA8}) {
        if (!rhi->isTextureFormatSupported(format)) {
            continue;
        }
        m_target.reset();
        m_texture.reset(rhi->newTexture(format, size, 1, QRhiTexture::RenderTarget));
        if (!m_texture->create()) {
            continue;
        }
        m_target.reset(rhi->newTextureRenderTarget({QRhiColorAttachment(m_texture.get())}));
        m_renderPass.reset(m_target->newCompatibleRenderPassDescriptor());
        m_target->setRenderPassDescriptor(m_renderPass.get());
        if (m_target->create()) {
            return true;
        }
    }
    return false;
}

void QcAmbientOcclusion::upload(QRhiResourceUpdateBatch *updates, const QMatrix4x4 &viewProjection) const
{
    // A result implies m_built; the check states it for the reader and the analysis.
    if (!result() || !m_built) {
        return;
    }

    OcclusionUniforms uniforms{};
    std::copy_n(viewProjection.constData(), 16, uniforms.viewProjection);
    const QMatrix4x4 inverse = viewProjection.inverted();
    std::copy_n(inverse.constData(), 16, uniforms.inverseViewProjection);
    uniforms.params[0] = radius;
    uniforms.params[1] = strength;
    uniforms.params[2] = static_cast<float>(m_samples);
    uniforms.params[3] = m_depthZeroToOne ? 1.0f : 0.0f;
    uniforms.view[0] = fadeStart;
    uniforms.view[1] = fadeEnd;
    uniforms.view[2] = nearest;
    uniforms.view[3] = m_uvMatchesNdcY ? 1.0f : -1.0f;
    const QSize depth = m_built->size;
    const QSize target = m_texture->pixelSize();
    uniforms.texel[0] = 1.0f / static_cast<float>(depth.width());
    uniforms.texel[1] = 1.0f / static_cast<float>(depth.height());
    uniforms.texel[2] = 1.0f / static_cast<float>(target.width());
    uniforms.texel[3] = 1.0f / static_cast<float>(target.height());
    updates->updateDynamicBuffer(m_uniforms.get(), 0, sizeof(OcclusionUniforms), &uniforms);
}

void QcAmbientOcclusion::draw(QRhiCommandBuffer *commandBuffer) const
{
    if (!result()) {
        return;
    }

    const QSize size = m_texture->pixelSize();
    commandBuffer->beginPass(m_target.get(), Qt::white, {1.0f, 0});
    commandBuffer->setGraphicsPipeline(m_pipeline.get());
    commandBuffer->setViewport(QRhiViewport(0, 0, static_cast<float>(size.width()), static_cast<float>(size.height())));
    commandBuffer->setShaderResources(m_bindings.get());
    commandBuffer->draw(3);
    commandBuffer->endPass();
}

void QcAmbientOcclusion::reset()
{
    releaseResources();
    m_built.reset();
}

void QcAmbientOcclusion::releaseResources()
{
    // Pipeline and bindings before what they refer to, the target before its texture.
    m_pipeline.reset();
    m_bindings.reset();
    m_target.reset();
    m_renderPass.reset();
    m_texture.reset();
    m_sampler.reset();
    m_uniforms.reset();
}
