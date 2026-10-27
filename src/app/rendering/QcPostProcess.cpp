#include "app/rendering/QcPostProcess.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcRenderPipelines.h"
#include "clientcore/light/QcWaterLook.h"

#include <QImage>

#include <algorithm>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;
using QcRenderPipelines::loadShader;

namespace {

// std140 layout of the Post block of composite.frag.
struct CompositeUniforms
{
    float inverseViewProjection[16];
    // rgb: fog colour, w: height fog density at its base.
    float fogColor[4];
    // x: camera height, y: fog base, z: falloff, w: farthest fog distance.
    float fog[4];
    // xyz: towards the sun, w: fog glow towards it.
    float sun[4];
    // x: exposure, y: bloom strength, z: 1 for clip depth 0..1.
    float tone[4];
    // xy: one output pixel in texture coordinates, z: where the height fog begins, w: 1 with an
    // ambient occlusion.
    float size[4];
    // xy: one texel of the ambient occlusion in texture coordinates.
    float occlusionTexel[4];
    // rgb: the water's colour under water (display values), w: 1 when the camera is under water.
    float water[4];
    // rgb: how much light the water takes per block (QcWaterLook::extinction).
    float waterExtinction[4];
};
static_assert(sizeof(CompositeUniforms) == 192);

// The first of `formats` the GPU renders to, if any.
std::optional<QRhiTexture::Format> supported(QRhi *rhi, std::initializer_list<QRhiTexture::Format> formats)
{
    for (const QRhiTexture::Format format : formats) {
        if (rhi->isTextureFormatSupported(format)) {
            return format;
        }
    }
    return std::nullopt;
}

} // namespace

bool QcPostProcess::prepare(QRhi *rhi, QSize size, QcRenderOptions options, QRhiRenderTarget *output, QRhiResourceUpdateBatch *updates)
{
    const Request request{rhi, size, output->renderPassDescriptor(), output->sampleCount()};
    if (m_refused == request) {
        return false;
    }

    m_refused.reset();
    // A batch of its own, merged into the frame's only on success: a refused part is released at
    // once, and the frame's batch must not refer to it.
    QRhiResourceUpdateBatch *own = rhi->nextResourceUpdateBatch();
    const bool ready = build(rhi, size, options, output, own);
    if (ready) {
        updates->merge(own);
    }
    own->release();
    if (!ready) {
        release();
        m_refused = request;
    }
    return ready;
}

bool QcPostProcess::build(QRhi *rhi, QSize size, QcRenderOptions options, QRhiRenderTarget *output, QRhiResourceUpdateBatch *own)
{
    if (size.isEmpty()) {
        return false;
    }
    if (!m_gpu.uniforms && !createShared(rhi, own)) {
        qCWarning(lcRenderer) << "Post-processing: the GPU refused its buffers; the realistic mode is not drawn";
        return false;
    }
    if (!m_gpu.sceneColour || m_gpu.sceneColour->pixelSize() != size) {
        // The bloom, the occlusion and the composite read the old textures.
        m_gpu.pipeline.reset();
        m_gpu.bindings.reset();
        m_gpu.bloom.reset();
        m_gpu.occlusion.reset();
        if (!createScene(rhi, size)) {
            return false;
        }
    }

    const auto [bloomGeneration, occlusionGeneration] = prepareEffects(rhi, options, own);
    if (!m_gpu.bindings || bloomGeneration != m_gpu.boundGeneration || occlusionGeneration != m_gpu.boundOcclusion) {
        m_gpu.pipeline.reset();
        m_gpu.boundGeneration = bloomGeneration;
        m_gpu.boundOcclusion = occlusionGeneration;
        if (!bindComposite(rhi)) {
            return false;
        }
    }
    if (!m_gpu.pipeline || m_gpu.outputSampleCount != output->sampleCount()
        || !m_gpu.outputPass->isCompatible(output->renderPassDescriptor())) {
        return createCompositePipeline(rhi, output);
    }
    return true;
}

std::pair<quint64, quint64> QcPostProcess::prepareEffects(QRhi *rhi, QcRenderOptions options, QRhiResourceUpdateBatch *own)
{
    // Without an HDR scene nothing lies above white, so there is nothing to bloom.
    const int levels = m_gpu.sceneFormat == QRhiTexture::RGBA16F ? options.bloomLevels() : 0;
    if (levels == 0) {
        m_gpu.bloom.reset();
    }
    const bool blooms = levels > 0 && m_gpu.bloom.prepare(rhi, m_gpu.sceneColour.get(), levels, m_gpu.sceneFormat, own);

    const int samples = options.occlusionSamples();
    if (samples == 0) {
        m_gpu.occlusion.reset();
    }
    const bool occludes = samples > 0 && m_gpu.occlusion.prepare(rhi, m_gpu.sceneDepth.get(), samples);
    return {blooms ? m_gpu.bloom.generation() : 0, occludes ? m_gpu.occlusion.generation() : 0};
}

bool QcPostProcess::createShared(QRhi *rhi, QRhiResourceUpdateBatch *own)
{
    m_gpu.uniforms.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(CompositeUniforms)));
    m_gpu.linear.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                       QRhiSampler::ClampToEdge));
    m_gpu.nearest.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                        QRhiSampler::ClampToEdge));
    m_gpu.noBloom.reset(rhi->newTexture(QRhiTexture::RGBA8, {1, 1}));
    m_gpu.noOcclusion.reset(rhi->newTexture(QRhiTexture::RGBA8, {1, 1}));
    // On failure prepare() releases all of them.
    if (!m_gpu.uniforms->create() || !m_gpu.linear->create() || !m_gpu.nearest->create() || !m_gpu.noBloom->create()
        || !m_gpu.noOcclusion->create()) {
        return false;
    }
    QImage black(1, 1, QImage::Format_RGBA8888);
    black.fill(Qt::black);
    own->uploadTexture(m_gpu.noBloom.get(), black);
    QImage white(1, 1, QImage::Format_RGBA8888);
    white.fill(Qt::white);
    own->uploadTexture(m_gpu.noOcclusion.get(), white);
    return true;
}

bool QcPostProcess::createScene(QRhi *rhi, QSize size)
{
    const std::optional<QRhiTexture::Format> depth = supported(rhi, {QRhiTexture::D32F, QRhiTexture::D24, QRhiTexture::D16});
    if (!depth) {
        qCWarning(lcRenderer) << "Post-processing: no depth format to sample; the realistic mode is not drawn";
        return false;
    }
    // HDR first; a GPU that refuses it as a target still gets the fog and the tonemapping in RGBA8.
    for (const QRhiTexture::Format colour : {QRhiTexture::RGBA16F, QRhiTexture::RGBA8}) {
        if (!rhi->isTextureFormatSupported(colour)) {
            continue;
        }
        if (createSceneTarget(rhi, size, colour, *depth)) {
            if (colour != QRhiTexture::RGBA16F) {
                qCInfo(lcRenderer) << "Post-processing: no HDR target on this GPU; the scene is drawn in RGBA8 without bloom";
            }
            return true;
        }
    }
    qCWarning(lcRenderer) << "Post-processing: the GPU refused the scene target of" << size << "; the realistic mode is not drawn";
    return false;
}

bool QcPostProcess::createSceneTarget(QRhi *rhi, QSize size, QRhiTexture::Format colour, QRhiTexture::Format depth)
{
    // The target before the textures it refers to; the pass descriptor stays while the format does,
    // so the pipelines built for it stay valid over a resize.
    m_gpu.sceneContinued.reset();
    m_gpu.sceneTarget.reset();
    if (colour != m_gpu.sceneFormat) {
        m_gpu.continuedPass.reset();
        m_gpu.scenePass.reset();
    }
    m_gpu.sceneFormat = colour;
    m_gpu.depthZeroToOne = rhi->isClipDepthZeroToOne();
    m_gpu.sceneColour.reset(rhi->newTexture(colour, size, 1, QRhiTexture::RenderTarget));
    m_gpu.sceneDepth.reset(rhi->newTexture(depth, size, 1, QRhiTexture::RenderTarget));
    if (!m_gpu.sceneColour->create() || !m_gpu.sceneDepth->create()) {
        m_gpu.sceneColour.reset();
        m_gpu.sceneDepth.reset();
        return false;
    }

    QRhiTextureRenderTargetDescription description{QRhiColorAttachment(m_gpu.sceneColour.get())};
    description.setDepthTexture(m_gpu.sceneDepth.get());
    m_gpu.sceneTarget.reset(rhi->newTextureRenderTarget(description));
    if (!m_gpu.scenePass) {
        m_gpu.scenePass.reset(m_gpu.sceneTarget->newCompatibleRenderPassDescriptor());
    }
    m_gpu.sceneTarget->setRenderPassDescriptor(m_gpu.scenePass.get());
    if (!m_gpu.sceneTarget->create() || !createContinuedTarget(rhi, description)) {
        m_gpu.sceneContinued.reset();
        m_gpu.sceneTarget.reset();
        m_gpu.sceneColour.reset();
        m_gpu.sceneDepth.reset();
        return false;
    }
    ++m_sceneGeneration;
    return true;
}

bool QcPostProcess::createContinuedTarget(QRhi *rhi, const QRhiTextureRenderTargetDescription &description)
{
    // Loads colour and depth instead of clearing them; the pipelines built for the scene's pass draw
    // into it: the passes differ only in their load operations, which the Vulkan specification
    // leaves out of compatibility (QRhi's isCompatible() may still say no there).
    m_gpu.sceneContinued.reset(rhi->newTextureRenderTarget(
        description, QRhiTextureRenderTarget::PreserveColorContents | QRhiTextureRenderTarget::PreserveDepthStencilContents));
    if (!m_gpu.continuedPass) {
        m_gpu.continuedPass.reset(m_gpu.sceneContinued->newCompatibleRenderPassDescriptor());
    }
    m_gpu.sceneContinued->setRenderPassDescriptor(m_gpu.continuedPass.get());
    return m_gpu.sceneContinued->create();
}

bool QcPostProcess::bindComposite(QRhi *rhi)
{
    QRhiTexture *const bloom = m_gpu.boundGeneration != 0 ? m_gpu.bloom.result() : m_gpu.noBloom.get();
    QRhiTexture *const occlusion = m_gpu.boundOcclusion != 0 ? m_gpu.occlusion.result() : m_gpu.noOcclusion.get();
    constexpr auto stage = QRhiShaderResourceBinding::FragmentStage;
    m_gpu.bindings.reset(rhi->newShaderResourceBindings());
    m_gpu.bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, stage, m_gpu.uniforms.get()),
                                 QRhiShaderResourceBinding::sampledTexture(1, stage, m_gpu.sceneColour.get(), m_gpu.nearest.get()),
                                 QRhiShaderResourceBinding::sampledTexture(2, stage, m_gpu.sceneDepth.get(), m_gpu.nearest.get()),
                                 QRhiShaderResourceBinding::sampledTexture(3, stage, bloom, m_gpu.linear.get()),
                                 QRhiShaderResourceBinding::sampledTexture(4, stage, occlusion, m_gpu.linear.get())});
    if (!m_gpu.bindings->create()) {
        m_gpu.bindings.reset();
        qCWarning(lcRenderer) << "Post-processing: the GPU refused the composite's bindings";
        return false;
    }
    return true;
}

bool QcPostProcess::createCompositePipeline(QRhi *rhi, QRhiRenderTarget *output)
{
    m_gpu.pipeline.reset(rhi->newGraphicsPipeline());
    m_gpu.pipeline->setShaderStages({{QRhiShaderStage::Vertex, loadShader(u":/shaders/post.vert.qsb"_s, lcRenderer())},
                                     {QRhiShaderStage::Fragment, loadShader(u":/shaders/composite.frag.qsb"_s, lcRenderer())}});
    // No vertex buffer: the shader makes the triangle from the vertex index.
    m_gpu.pipeline->setVertexInputLayout({});
    m_gpu.pipeline->setShaderResourceBindings(m_gpu.bindings.get());
    m_gpu.pipeline->setRenderPassDescriptor(output->renderPassDescriptor());
    m_gpu.pipeline->setSampleCount(output->sampleCount());
    if (!m_gpu.pipeline->create()) {
        m_gpu.pipeline.reset();
        qCCritical(lcRenderer) << "Cannot create the composite pipeline";
        return false;
    }
    m_gpu.outputPass.reset(output->renderPassDescriptor()->newCompatibleRenderPassDescriptor());
    m_gpu.outputSampleCount = output->sampleCount();
    return true;
}

void QcPostProcess::uploadComposite(QRhiResourceUpdateBatch *updates, const Composite &composite) const
{
    if (!m_gpu.uniforms || !m_gpu.sceneColour) {
        return;
    }

    CompositeUniforms uniforms{};
    const QMatrix4x4 inverse = composite.viewProjection.inverted();
    std::copy_n(inverse.constData(), 16, uniforms.inverseViewProjection);
    uniforms.fogColor[0] = composite.fogColor.x();
    uniforms.fogColor[1] = composite.fogColor.y();
    uniforms.fogColor[2] = composite.fogColor.z();
    uniforms.fogColor[3] = composite.fogDensity;
    uniforms.fog[0] = composite.cameraHeight;
    uniforms.fog[1] = composite.fogBase;
    uniforms.fog[2] = fogFalloff;
    uniforms.fog[3] = composite.fogDistance;
    uniforms.sun[0] = composite.sunDirection.x();
    uniforms.sun[1] = composite.sunDirection.y();
    uniforms.sun[2] = composite.sunDirection.z();
    uniforms.sun[3] = composite.sunGlow;
    uniforms.tone[0] = composite.exposure * baseExposure;
    uniforms.tone[1] = blooms() ? bloomStrength : 0.0f;
    uniforms.tone[2] = m_gpu.depthZeroToOne ? 1.0f : 0.0f;
    const QSize size = m_gpu.sceneColour->pixelSize();
    uniforms.size[0] = 1.0f / static_cast<float>(size.width());
    uniforms.size[1] = 1.0f / static_cast<float>(size.height());
    uniforms.size[2] = fogStart;
    uniforms.size[3] = occludes() ? 1.0f : 0.0f;
    if (const QRhiTexture *occlusion = occludes() ? m_gpu.occlusion.result() : nullptr) {
        uniforms.occlusionTexel[0] = 1.0f / static_cast<float>(occlusion->pixelSize().width());
        uniforms.occlusionTexel[1] = 1.0f / static_cast<float>(occlusion->pixelSize().height());
        m_gpu.occlusion.upload(updates, composite.viewProjection);
    }
    uniforms.water[0] = composite.waterColour.x();
    uniforms.water[1] = composite.waterColour.y();
    uniforms.water[2] = composite.waterColour.z();
    uniforms.water[3] = composite.underwater ? 1.0f : 0.0f;
    std::ranges::copy(QcWaterLook::extinction, uniforms.waterExtinction);
    updates->updateDynamicBuffer(m_gpu.uniforms.get(), 0, sizeof(CompositeUniforms), &uniforms);
}

void QcPostProcess::drawComposite(QRhiCommandBuffer *commandBuffer) const
{
    if (!m_gpu.pipeline) {
        return;
    }
    commandBuffer->setGraphicsPipeline(m_gpu.pipeline.get());
    commandBuffer->setShaderResources(m_gpu.bindings.get());
    commandBuffer->draw(3);
}

void QcPostProcess::release()
{
    // Handed out whole, so its destructor releases the members in reverse order.
    const Resources retired = std::exchange(m_gpu, {});
}

void QcPostProcess::reset()
{
    release();
    m_refused.reset();
}
