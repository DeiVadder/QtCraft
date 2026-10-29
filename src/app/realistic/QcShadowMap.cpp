// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/realistic/QcShadowMap.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcRenderPipelines.h"
#include "app/rendering/QcWorldRendererResources.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

using namespace Qt::StringLiterals;
using QcRenderPipelines::loadShader;

namespace {

constexpr int maxCascades = QcRenderOptions::maxShadowCascades;

// std140 layout of the Shadow block of block.frag and entity.frag (QC_SHADOWS).
struct ReceiverUniforms
{
    float cascades[maxCascades][16];
    // x: offset along the normal, y: towards the light (blocks).
    float offsets[maxCascades][4];
    // xyz: towards the light, w: strength.
    float toLight[4];
    // rgb: lit colour, w: shadow floor.
    float litTint[4];
    // rgb: shadow fill colour, w: cascade count.
    float shadowTint[4];
    // xy: one texel of the map, z: one texel of a tile (texture coordinates), w: lookups per pixel.
    float texel[4];
    // x, y: where the shadows begin and end to fade out (blocks from the camera).
    float fade[4];
    // The material maps (decision E-908): x 1 while they are bound, y 1 for the moving lights'
    // highlights, z the strength of the relief, w of the highlights and reflections.
    float material[4];
};
static_assert(sizeof(ReceiverUniforms) == 336);

// std140 layout of the Caster block of shadow.vert and shadow_entity.vert.
struct CasterUniforms
{
    float lightViewProjection[16];
    float drawCamera[4];
};
static_assert(sizeof(CasterUniforms) == 80);

// The slope-scaled depth bias of the casters: faces the light grazes keep clear of their own
// shadow; the receivers' offsets do the rest.
constexpr float casterSlopeBias = 1.0f;

// How far the filter of `taps` lookups reaches from the pixel's texel, in texels.
float filterRadius(int taps)
{
    return taps >= 9 ? 1.5f : taps >= 4 ? 1.0f : 0.5f;
}

std::array<float, 4> packed(QVector3D value, float w)
{
    return {value.x(), value.y(), value.z(), w};
}

std::unique_ptr<QRhiGraphicsPipeline> casterPipeline(QRhi *rhi, QRhiRenderPassDescriptor *pass, QRhiShaderResourceBindings *bindings,
                                                     const QRhiVertexInputLayout &layout, const std::pair<QString, QString> &shaders)
{
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, loadShader(shaders.first, lcRenderer())},
                               {QRhiShaderStage::Fragment, loadShader(shaders.second, lcRenderer())}});
    pipeline->setVertexInputLayout(layout);
    pipeline->setShaderResourceBindings(bindings);
    pipeline->setRenderPassDescriptor(pass);
    pipeline->setDepthTest(true);
    pipeline->setDepthWrite(true);
    // Both sides: plants and leaves are single quads, and a cube seen edge-on still casts.
    pipeline->setCullMode(QRhiGraphicsPipeline::None);
    pipeline->setSlopeScaledDepthBias(casterSlopeBias);
    if (!pipeline->create()) {
        qCWarning(lcRenderer) << "Sun shadows: the GPU refused a caster pipeline";
        return {};
    }
    return pipeline;
}

} // namespace

bool QcShadowMap::replaces(QRhi *rhi, QcRenderOptions::Shadows shadows) const
{
    return holdsResources()
           && (rhi != m_gpu.rhi || shadows.cascades != m_gpu.shadows.cascades || shadows.resolution != m_gpu.shadows.resolution);
}

bool QcShadowMap::prepare(QRhi *rhi, QcRenderOptions::Shadows shadows)
{
    const Request request{rhi, shadows};
    if (refused() == request) {
        return false;
    }
    if (shadows.cascades < 1 || shadows.cascades > maxCascades || shadows.resolution < 1) {
        release();
        return false;
    }

    setRefused(std::nullopt);
    // Filter and distance change no resource.
    if (ready() && !replaces(rhi, shadows)) {
        m_gpu.shadows = shadows;
        return true;
    }
    release();
    if (!build(rhi, shadows)) {
        release();
        setRefused(request);
        return false;
    }
    ++m_generation;
    return true;
}

bool QcShadowMap::build(QRhi *rhi, QcRenderOptions::Shadows shadows)
{
    m_gpu.rhi = rhi;
    m_gpu.shadows = shadows;
    if (!createBuffers(rhi, shadows.cascades)) {
        qCWarning(lcRenderer) << "Sun shadows: the GPU refused their buffers; the realistic mode draws without them";
        return false;
    }
    return createMap(rhi, shadows);
}

bool QcShadowMap::createBuffers(QRhi *rhi, int cascades)
{
    const auto alignment = static_cast<quint32>(rhi->ubufAlignment());
    m_gpu.casterStride = (static_cast<quint32>(sizeof(CasterUniforms)) + alignment - 1) / alignment * alignment;
    m_gpu.receiverUniforms.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(ReceiverUniforms)));
    m_gpu.casterUniforms.reset(
        rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, m_gpu.casterStride * static_cast<quint32>(cascades)));
    // Linear with comparison: each lookup filters 2 x 2 texels of the map.
    m_gpu.compare.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                        QRhiSampler::ClampToEdge));
    m_gpu.compare->setTextureCompareOp(QRhiSampler::LessOrEqual);
    return m_gpu.receiverUniforms->create() && m_gpu.casterUniforms->create() && m_gpu.compare->create();
}

bool QcShadowMap::createMap(QRhi *rhi, QcRenderOptions::Shadows shadows)
{
    const QSize size(shadows.resolution * shadows.cascades, shadows.resolution);
    if (size.width() > rhi->resourceLimit(QRhi::TextureSizeMax)) {
        qCWarning(lcRenderer) << "Sun shadows: a map of" << size << "is larger than the GPU allows";
        return false;
    }

    // 16 bits hold the depth of an orthographic box a few hundred blocks deep to a hundredth of a
    // block, at half the memory of 32.
    for (const QRhiTexture::Format format : {QRhiTexture::D16, QRhiTexture::D32F, QRhiTexture::D24}) {
        if (!rhi->isTextureFormatSupported(format)) {
            continue;
        }
        m_gpu.map.reset(rhi->newTexture(format, size, 1, QRhiTexture::RenderTarget));
        if (!m_gpu.map->create()) {
            m_gpu.map.reset();
            continue;
        }
        m_gpu.format = format;
        QRhiTextureRenderTargetDescription description;
        description.setDepthTexture(m_gpu.map.get());
        m_gpu.target.reset(rhi->newTextureRenderTarget(description));
        m_gpu.pass.reset(m_gpu.target->newCompatibleRenderPassDescriptor());
        m_gpu.target->setRenderPassDescriptor(m_gpu.pass.get());
        if (m_gpu.target->create()) {
            return true;
        }
        m_gpu.target.reset();
        m_gpu.pass.reset();
        m_gpu.map.reset();
    }
    qCWarning(lcRenderer) << "Sun shadows: the GPU refused a shadow map of" << size << "; the realistic mode draws without them";
    return false;
}

void QcShadowMap::upload(QRhiResourceUpdateBatch *updates, const Frame &frame)
{
    if (!ready()) {
        return;
    }

    const QRhi *const rhi = m_gpu.rhi;
    const int count = cascadeCount();
    const QcShadowCascades::Radii radii = QcShadowCascades::radii(count, m_gpu.shadows.distance);
    const std::optional<QcShadowCascades::Basis> axes = QcShadowCascades::basis(frame.light.toLight);
    const bool finite = std::isfinite(frame.camera.x) && std::isfinite(frame.camera.y) && std::isfinite(frame.camera.z);
    const bool casts = axes && finite && radii.front() > 0.0f;
    const QMatrix4x4 correction = rhi->clipSpaceCorrMatrix();
    const QMatrix4x4 texture = QcShadowCascades::textureMatrix(rhi->isYUpInFramebuffer() != rhi->isYUpInNDC(), rhi->isClipDepthZeroToOne());
    const float reach = 1.0f + filterRadius(m_gpu.shadows.filterTaps);

    ReceiverUniforms receivers{};
    for (int i = 0; casts && i < count; ++i) {
        const auto index = static_cast<std::size_t>(i);
        const QcShadowCascade cascade =
            QcShadowCascades::fit(*axes, frame.camera, frame.forward, radii[index], m_gpu.shadows.resolution);
        setCasterMatrix(i, correction * cascade.lightViewProjection);
        std::copy_n((texture * casterMatrix(i)).constData(), 16, receivers.cascades[index]);
        receivers.offsets[index][0] = cascade.texelSize * reach;
        receivers.offsets[index][1] = cascade.texelSize * 0.5f;

        CasterUniforms caster{};
        std::copy_n(casterMatrix(i).constData(), 16, caster.lightViewProjection);
        std::ranges::copy(packed(frame.drawCamera, 0.0f), caster.drawCamera);
        updates->updateDynamicBuffer(m_gpu.casterUniforms.get(), static_cast<quint32>(i) * m_gpu.casterStride, sizeof(CasterUniforms),
                                     &caster);
    }

    // NaN in the light or the camera: no shadows rather than a broken picture.
    const float strength = casts && std::isfinite(frame.light.strength) ? std::clamp(frame.light.strength, 0.0f, 1.0f) : 0.0f;
    std::ranges::copy(packed(frame.light.toLight, strength), receivers.toLight);
    std::ranges::copy(packed(frame.light.litTint, shadowFloor), receivers.litTint);
    std::ranges::copy(packed(frame.light.shadowTint, static_cast<float>(count)), receivers.shadowTint);
    const auto resolution = static_cast<float>(m_gpu.shadows.resolution);
    receivers.texel[0] = 1.0f / (resolution * static_cast<float>(count));
    receivers.texel[1] = 1.0f / resolution;
    receivers.texel[2] = 1.0f / resolution;
    receivers.texel[3] = static_cast<float>(m_gpu.shadows.filterTaps);
    receivers.fade[0] = fadeStart * m_gpu.shadows.distance;
    receivers.fade[1] = m_gpu.shadows.distance;
    // Checked here, not in the shader (fast GPU maths may drop isnan): a value that is not finite is 0.
    for (std::size_t i = 0; i < frame.material.size(); ++i) {
        receivers.material[i] = std::isfinite(frame.material[i]) ? frame.material[i] : 0.0f;
    }
    updates->updateDynamicBuffer(m_gpu.receiverUniforms.get(), 0, sizeof(ReceiverUniforms), &receivers);
    setCasts(strength > 0.0f);
}

const QMatrix4x4 &QcShadowMap::casterMatrix(int index) const
{
    return m_casterMatrices.at(static_cast<std::size_t>(index));
}

QRhiCommandBuffer::DynamicOffset QcShadowMap::casterOffset(int index) const
{
    return {0, static_cast<quint32>(index) * m_gpu.casterStride};
}

QRhiShaderResourceBindings *QcShadowMap::receiverBindings(const QcWorldDeviceResources &device, const QList<QRhiShaderResourceBinding> &materials)
{
    if (m_gpu.receiverBindings) {
        return m_gpu.receiverBindings.get();
    }
    if (!ready() || !device.hasAtlas() || !device.frameUniforms) {
        return nullptr;
    }

    constexpr auto vertexAndFragment = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
    constexpr auto fragment = QRhiShaderResourceBinding::FragmentStage;
    QList<QRhiShaderResourceBinding> entries{
        QRhiShaderResourceBinding::uniformBuffer(0, vertexAndFragment, device.frameUniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, fragment, device.atlasTexture.get(), device.atlasSampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::VertexStage, device.animationTexture.get(),
                                                  device.animationSampler.get()),
        QRhiShaderResourceBinding::uniformBuffer(3, fragment, m_gpu.receiverUniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(4, fragment, m_gpu.map.get(), m_gpu.compare.get()),
    };
    entries.append(materials);
    m_gpu.receiverBindings.reset(m_gpu.rhi->newShaderResourceBindings());
    m_gpu.receiverBindings->setBindings(entries.cbegin(), entries.cend());
    if (!m_gpu.receiverBindings->create()) {
        qCWarning(lcRenderer) << "Sun shadows: the GPU refused the receivers' bindings; the realistic mode draws without them";
        const Request request{m_gpu.rhi, m_gpu.shadows};
        release();
        setRefused(request);
        return nullptr;
    }
    return m_gpu.receiverBindings.get();
}

QList<QRhiShaderResourceBinding> QcShadowMap::receiverEntries() const
{
    if (!ready()) {
        return {};
    }
    constexpr auto fragment = QRhiShaderResourceBinding::FragmentStage;
    return {QRhiShaderResourceBinding::uniformBuffer(3, fragment, m_gpu.receiverUniforms.get()),
            QRhiShaderResourceBinding::sampledTexture(4, fragment, m_gpu.map.get(), m_gpu.compare.get())};
}

QRhiShaderResourceBindings *QcShadowMap::framedMapBindings(const QcWorldDeviceResources &device, const QList<QRhiShaderResourceBinding> &materials)
{
    if (!m_gpu.framedMapBindings && ready() && device.framedMaps.bindings()) {
        m_gpu.framedMapBindings = device.framedMaps.bindingsWith(m_gpu.rhi, receiverEntries() + materials);
    }
    return m_gpu.framedMapBindings.get();
}

std::optional<QcShadowMap::Casters> QcShadowMap::casters(const QcWorldDeviceResources &device)
{
    if (!ready()) {
        return std::nullopt;
    }
    if (!m_gpu.castersTried && device.hasAtlas()) {
        m_gpu.castersTried = true;
        if (!createCasters(device)) {
            m_gpu.solidCaster.reset();
            m_gpu.cutoutCaster.reset();
            m_gpu.entityCaster.reset();
        }
    }
    if (!m_gpu.solidCaster || !m_gpu.cutoutCaster || !m_gpu.entityCaster) {
        return std::nullopt;
    }
    return Casters{m_gpu.solidCaster.get(), m_gpu.cutoutCaster.get(), m_gpu.entityCaster.get(), m_gpu.casterBindings.get()};
}

bool QcShadowMap::createCasters(const QcWorldDeviceResources &device)
{
    QRhi *const rhi = m_gpu.rhi;
    m_gpu.casterBindings.reset(rhi->newShaderResourceBindings());
    m_gpu.casterBindings->setBindings({
        QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0, QRhiShaderResourceBinding::VertexStage, m_gpu.casterUniforms.get(),
                                                                  sizeof(CasterUniforms)),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, device.atlasTexture.get(),
                                                  device.atlasSampler.get()),
    });
    if (!m_gpu.casterBindings->create()) {
        qCWarning(lcRenderer) << "Sun shadows: the GPU refused the casters' bindings";
        return false;
    }

    const QRhiVertexInputLayout blocks = QcRenderPipelines::blockLayout();
    QRhiShaderResourceBindings *const bindings = m_gpu.casterBindings.get();
    QRhiRenderPassDescriptor *const pass = m_gpu.pass.get();
    m_gpu.solidCaster = casterPipeline(rhi, pass, bindings, blocks, {u":/shaders/shadow.vert.qsb"_s, u":/shaders/shadow_solid.frag.qsb"_s});
    m_gpu.cutoutCaster = casterPipeline(rhi, pass, bindings, blocks, {u":/shaders/shadow.vert.qsb"_s, u":/shaders/shadow.frag.qsb"_s});
    m_gpu.entityCaster = casterPipeline(rhi, pass, bindings, QcRenderPipelines::entityLayout(),
                                        {u":/shaders/shadow_entity.vert.qsb"_s, u":/shaders/shadow.frag.qsb"_s});
    return m_gpu.solidCaster && m_gpu.cutoutCaster && m_gpu.entityCaster;
}

void QcShadowMap::releaseAtlasBindings()
{
    // Pipelines before the bindings they were built with.
    m_gpu.entityCaster.reset();
    m_gpu.cutoutCaster.reset();
    m_gpu.solidCaster.reset();
    m_gpu.casterBindings.reset();
    m_gpu.framedMapBindings.reset();
    m_gpu.receiverBindings.reset();
    m_gpu.castersTried = false;
}

void QcShadowMap::release()
{
    // Handed out whole, so its destructor releases the members in reverse order.
    const Resources retired = std::exchange(m_gpu, {});
    setCasts(false);
}

void QcShadowMap::reset()
{
    release();
    setRefused(std::nullopt);
}
