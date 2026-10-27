#include "app/rendering/QcWaterSurface.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcPostProcess.h"
#include "app/rendering/QcRenderPipelines.h"
#include "app/rendering/QcShadowMap.h"
#include "app/rendering/QcWorldRendererResources.h"

#include <utility>

using namespace Qt::StringLiterals;
using QcRenderPipelines::loadShader;

namespace {

// The bindings of the water's shaders beyond the atlas's and the shadow receivers'.
constexpr int behindBinding = 5;
constexpr int uniformBinding = 6;

} // namespace

bool QcWaterSurface::prepare(QRhi *rhi, const QcPostProcess &post, QcRenderOptions::Water water)
{
    if (!water.drawn() || !post.sceneColour()) {
        release();
        return false;
    }
    const Request request{rhi, post.sceneGeneration()};
    if (refused() == request) {
        return false;
    }
    if (ready() && m_gpu.rhi == rhi && m_gpu.scene == request.scene) {
        return true;
    }

    setRefused(std::nullopt);
    if (!build(rhi, post)) {
        release();
        setRefused(request);
        return false;
    }
    return true;
}

bool QcWaterSurface::build(QRhi *rhi, const QcPostProcess &post)
{
    // Another QRhi starts from nothing; another scene keeps the buffer and the samplers.
    if (m_gpu.rhi != rhi) {
        release();
    }
    m_gpu.rhi = rhi;
    if (!m_gpu.uniforms && !createShared(rhi)) {
        qCWarning(lcRenderer) << "Water: the GPU refused its buffer; the realistic mode draws the classic water";
        return false;
    }
    // The water's bindings and the copy's refer to the old copy and scene.
    m_gpu.waterPipeline.reset();
    m_gpu.waterBindings.reset();
    m_gpu.waterTried = false;
    m_gpu.copyPipeline.reset();
    m_gpu.copyBindings.reset();
    if (!createCopy(rhi, post)) {
        qCWarning(lcRenderer) << "Water: the GPU refused the copy of the scene; the realistic mode draws the classic water";
        return false;
    }
    m_gpu.scene = post.sceneGeneration();
    return true;
}

bool QcWaterSurface::createShared(QRhi *rhi)
{
    m_gpu.uniforms.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(QcWaterUniforms)));
    m_gpu.nearest.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                        QRhiSampler::ClampToEdge));
    m_gpu.linear.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                       QRhiSampler::ClampToEdge));
    // On failure prepare() releases all of them.
    return m_gpu.uniforms->create() && m_gpu.nearest->create() && m_gpu.linear->create();
}

bool QcWaterSurface::createCopy(QRhi *rhi, const QcPostProcess &post)
{
    // The distance needs more than eight bits; without a float target there is no copy.
    if (!rhi->isTextureFormatSupported(QRhiTexture::RGBA16F)) {
        return false;
    }
    const QSize size = post.sceneColour()->pixelSize();
    m_gpu.behindTarget.reset();
    m_gpu.behind.reset(rhi->newTexture(QRhiTexture::RGBA16F, size, 1, QRhiTexture::RenderTarget));
    if (!m_gpu.behind->create()) {
        return false;
    }
    m_gpu.behindTarget.reset(rhi->newTextureRenderTarget({QRhiColorAttachment(m_gpu.behind.get())}));
    if (!m_gpu.behindPass) {
        m_gpu.behindPass.reset(m_gpu.behindTarget->newCompatibleRenderPassDescriptor());
    }
    m_gpu.behindTarget->setRenderPassDescriptor(m_gpu.behindPass.get());
    if (!m_gpu.behindTarget->create()) {
        return false;
    }

    constexpr auto stage = QRhiShaderResourceBinding::FragmentStage;
    m_gpu.copyBindings.reset(rhi->newShaderResourceBindings());
    m_gpu.copyBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, stage, m_gpu.uniforms.get()),
                                     QRhiShaderResourceBinding::sampledTexture(1, stage, post.sceneColour(), m_gpu.nearest.get()),
                                     QRhiShaderResourceBinding::sampledTexture(2, stage, post.sceneDepth(), m_gpu.nearest.get())});
    if (!m_gpu.copyBindings->create()) {
        return false;
    }

    std::unique_ptr<QRhiGraphicsPipeline> pipeline(rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, loadShader(u":/shaders/post.vert.qsb"_s, lcRenderer())},
                               {QRhiShaderStage::Fragment, loadShader(u":/shaders/water_behind.frag.qsb"_s, lcRenderer())}});
    // No vertex buffer: the shader makes the triangle from the vertex index.
    pipeline->setVertexInputLayout({});
    pipeline->setShaderResourceBindings(m_gpu.copyBindings.get());
    pipeline->setRenderPassDescriptor(m_gpu.behindPass.get());
    if (!pipeline->create()) {
        return false;
    }
    m_gpu.copyPipeline = std::move(pipeline);
    return true;
}

void QcWaterSurface::upload(QRhiResourceUpdateBatch *updates, const QcWaterUniforms &uniforms) const
{
    if (m_gpu.uniforms) {
        updates->updateDynamicBuffer(m_gpu.uniforms.get(), 0, sizeof(QcWaterUniforms), &uniforms);
    }
}

void QcWaterSurface::drawCopy(QRhiCommandBuffer *commandBuffer) const
{
    if (!ready()) {
        return;
    }
    commandBuffer->beginPass(m_gpu.behindTarget.get(), Qt::black, {1.0f, 0});
    const QSize size = m_gpu.behind->pixelSize();
    commandBuffer->setViewport(QRhiViewport(0, 0, static_cast<float>(size.width()), static_cast<float>(size.height())));
    commandBuffer->setGraphicsPipeline(m_gpu.copyPipeline.get());
    commandBuffer->setShaderResources(m_gpu.copyBindings.get());
    commandBuffer->draw(3);
    commandBuffer->endPass();
}

std::optional<QcWaterSurface::Draw> QcWaterSurface::pipeline(const QcWorldDeviceResources &device, const QcShadowMap *shadows,
                                                             QRhiRenderTarget *target)
{
    if (!ready() || !target || !device.hasAtlas() || !device.frameUniforms) {
        return std::nullopt;
    }
    // Shadows that came, went or got another map, or another pass: the bindings and the pipeline
    // anew (the old bindings may refer to a released map).
    const quint64 shadowMap = shadows ? shadows->generation() : 0;
    if (m_gpu.waterTried && (m_gpu.waterShadowMap != shadowMap || m_gpu.waterPass != target->renderPassDescriptor())) {
        releaseAtlas();
    }
    if (!m_gpu.waterTried) {
        m_gpu.waterTried = true;
        m_gpu.waterShadowMap = shadowMap;
        m_gpu.waterPass = target->renderPassDescriptor();
        // The receivers' entries only here, when the bindings are made.
        const QList<QRhiShaderResourceBinding> shadowEntries = shadowMap != 0 ? shadows->receiverEntries() : QList<QRhiShaderResourceBinding>{};
        if (!createWater(device, shadowEntries, target)) {
            qCWarning(lcRenderer) << "Water: the GPU refused the water's pipeline; the realistic mode draws the classic water";
            m_gpu.waterPipeline.reset();
            m_gpu.waterBindings.reset();
        }
    }
    if (!m_gpu.waterPipeline) {
        return std::nullopt;
    }
    return Draw{m_gpu.waterPipeline.get(), m_gpu.waterBindings.get()};
}

bool QcWaterSurface::createWater(const QcWorldDeviceResources &device, const QList<QRhiShaderResourceBinding> &shadowEntries,
                                 QRhiRenderTarget *target)
{
    QRhi *const rhi = m_gpu.rhi;
    constexpr auto fragment = QRhiShaderResourceBinding::FragmentStage;
    QList<QRhiShaderResourceBinding> entries = {
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | fragment, device.frameUniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, fragment, device.atlasTexture.get(), device.atlasSampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::VertexStage, device.animationTexture.get(),
                                                  device.animationSampler.get()),
    };
    entries.append(shadowEntries);
    entries.append(QRhiShaderResourceBinding::sampledTexture(behindBinding, fragment, m_gpu.behind.get(), m_gpu.linear.get()));
    entries.append(QRhiShaderResourceBinding::uniformBuffer(uniformBinding, fragment, m_gpu.uniforms.get()));
    m_gpu.waterBindings.reset(rhi->newShaderResourceBindings());
    m_gpu.waterBindings->setBindings(entries.cbegin(), entries.cend());
    if (!m_gpu.waterBindings->create()) {
        return false;
    }

    // The block vertex shader of the shadow receivers hands on the light's parts and the face's
    // normal, with or without shadows.
    const QString fragmentShader = shadowEntries.isEmpty() ? u":/shaders/water.frag.qsb"_s : u":/shaders/water_shadow.frag.qsb"_s;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, loadShader(u":/shaders/block_shadow.vert.qsb"_s, lcRenderer())},
                               {QRhiShaderStage::Fragment, loadShader(fragmentShader, lcRenderer())}});
    pipeline->setVertexInputLayout(QcRenderPipelines::blockLayout());
    pipeline->setShaderResourceBindings(m_gpu.waterBindings.get());
    pipeline->setRenderPassDescriptor(target->renderPassDescriptor());
    pipeline->setSampleCount(target->sampleCount());
    // The water writes its depth (fog, ambient occlusion and the faces drawn after it see the
    // surface) and its whole colour: it mixes in what lies behind it itself. Both sides: the surface
    // is seen from below too.
    pipeline->setDepthTest(true);
    pipeline->setDepthWrite(true);
    pipeline->setCullMode(QRhiGraphicsPipeline::None);
    if (!pipeline->create()) {
        return false;
    }
    m_gpu.waterPipeline = std::move(pipeline);
    return true;
}

void QcWaterSurface::releaseAtlas()
{
    m_gpu.waterPipeline.reset();
    m_gpu.waterBindings.reset();
    m_gpu.waterTried = false;
}

void QcWaterSurface::release()
{
    // Handed out whole, so its destructor releases the members in reverse order.
    const Resources retired = std::exchange(m_gpu, {});
}

void QcWaterSurface::reset()
{
    release();
    m_refused.reset();
}
