// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/rendering/QcBloomChain.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcRenderPipelines.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <ranges>

using namespace Qt::StringLiterals;
using QcRenderPipelines::loadShader;

namespace {

// std140 layout of the Bloom block of bloom_down.frag and bloom_up.frag.
struct BloomUniforms
{
    // xy: one texel of the texture the pass reads (the smaller level when doubling), zw: one pixel
    // of its target.
    float texel[4];
    // x: threshold, y: knee, z: scatter, w: brightest value.
    float params[4];
};
static_assert(sizeof(BloomUniforms) == 32);

// The size of halving `level` (0: half the source), at least one pixel.
QSize halvedSize(QSize source, int level)
{
    return {std::max(source.width() >> (level + 1), 1), std::max(source.height() >> (level + 1), 1)};
}

BloomUniforms uniformsFor(QSize read, QSize target)
{
    return {{1.0f / static_cast<float>(read.width()), 1.0f / static_cast<float>(read.height()), 1.0f / static_cast<float>(target.width()),
             1.0f / static_cast<float>(target.height())},
            {QcBloomChain::threshold, QcBloomChain::knee, QcBloomChain::scatter, QcBloomChain::brightest}};
}

} // namespace

bool QcBloomChain::prepare(QRhi *rhi, QRhiTexture *source, int levels, QRhiTexture::Format format, QRhiResourceUpdateBatch *updates)
{
    // No more halvings than reach one pixel along the shorter side.
    const QSize sourceSize = source->pixelSize();
    int fitting = 0;
    while (fitting < levels && halvedSize(sourceSize, fitting).width() > 1 && halvedSize(sourceSize, fitting).height() > 1) {
        ++fitting;
    }
    // A chain the GPU refused is not tried again every frame.
    const Key key{source, sourceSize, fitting, format};
    if (m_built == key) {
        return result() != nullptr;
    }

    releaseResources();
    m_built = key;
    if (fitting == 0) {
        return false;
    }
    // The uniforms go into the frame's updates only when every part was made: a refused chain is
    // released at once, and the batch must not refer to its buffer.
    const std::optional<QByteArray> uniforms = createTextures(rhi, key) ? bindPasses(rhi, source) : std::nullopt;
    if (uniforms && createPipelines(rhi)) {
        updates->updateDynamicBuffer(m_uniforms.get(), 0, static_cast<quint32>(uniforms->size()), uniforms->constData());
        ++m_generation;
        return true;
    }

    qCWarning(lcRenderer) << "Bloom: the GPU refused a part of the chain; the realistic mode draws without bloom";
    releaseResources();
    return false;
}

bool QcBloomChain::createTextures(QRhi *rhi, const Key &key)
{
    const QSize sourceSize = key.size;
    const QRhiTexture::Format format = key.format;
    const auto makePass = [&](QSize size) -> std::optional<Pass> {
        Pass pass;
        pass.texture.reset(rhi->newTexture(format, size, 1, QRhiTexture::RenderTarget));
        if (!pass.texture->create()) {
            return std::nullopt;
        }
        pass.target.reset(rhi->newTextureRenderTarget({QRhiColorAttachment(pass.texture.get())}));
        if (!m_renderPass) {
            m_renderPass.reset(pass.target->newCompatibleRenderPassDescriptor());
        }
        pass.target->setRenderPassDescriptor(m_renderPass.get());
        if (!pass.target->create()) {
            return std::nullopt;
        }
        return pass;
    };

    for (int level = 0; level < key.levels; ++level) {
        std::optional<Pass> pass = makePass(halvedSize(sourceSize, level));
        if (!pass) {
            return false;
        }
        m_halvings.push_back(std::move(*pass));
    }
    // Doubling `level` draws at the size of halving `level`; the smallest halving has none.
    for (int level = 0; level + 1 < key.levels; ++level) {
        std::optional<Pass> pass = makePass(halvedSize(sourceSize, level));
        if (!pass) {
            return false;
        }
        m_doublings.push_back(std::move(*pass));
    }

    m_sampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                    QRhiSampler::ClampToEdge));
    return m_sampler->create();
}

std::optional<QByteArray> QcBloomChain::bindPasses(QRhi *rhi, QRhiTexture *source)
{
    const auto stride = static_cast<quint32>(rhi->ubufAligned(sizeof(BloomUniforms)));
    const auto passCount = static_cast<quint32>(m_halvings.size() + m_doublings.size());
    m_uniforms.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, stride * passCount));
    if (!m_uniforms->create()) {
        return std::nullopt;
    }
    QByteArray data(static_cast<qsizetype>(stride * passCount), '\0');

    constexpr auto stages = QRhiShaderResourceBinding::FragmentStage;
    quint32 offset = 0;
    const auto bind = [&](Pass &pass, QRhiTexture *read, QRhiTexture *second) {
        const BloomUniforms uniforms = uniformsFor(read->pixelSize(), pass.texture->pixelSize());
        std::memcpy(data.data() + offset, &uniforms, sizeof(BloomUniforms));
        QList<QRhiShaderResourceBinding> bindings{
            QRhiShaderResourceBinding::uniformBuffer(0, stages, m_uniforms.get(), offset, sizeof(BloomUniforms)),
            QRhiShaderResourceBinding::sampledTexture(1, stages, read, m_sampler.get())};
        if (second) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(2, stages, second, m_sampler.get()));
        }
        pass.bindings.reset(rhi->newShaderResourceBindings());
        pass.bindings->setBindings(bindings.cbegin(), bindings.cend());
        offset += stride;
        return pass.bindings->create();
    };

    for (std::size_t level = 0; level < m_halvings.size(); ++level) {
        QRhiTexture *read = level == 0 ? source : m_halvings[level - 1].texture.get();
        if (!bind(m_halvings[level], read, nullptr)) {
            return std::nullopt;
        }
    }
    for (std::size_t level = 0; level < m_doublings.size(); ++level) {
        const bool smallest = level + 1 == m_doublings.size();
        QRhiTexture *smaller = smallest ? m_halvings.back().texture.get() : m_doublings[level + 1].texture.get();
        if (!bind(m_doublings[level], smaller, m_halvings[level].texture.get())) {
            return std::nullopt;
        }
    }
    return data;
}

std::unique_ptr<QRhiGraphicsPipeline> QcBloomChain::createPipeline(QRhi *rhi, const QString &fragmentShader, int textures) const
{
    const Pass &layout = textures == 2 ? m_doublings.front() : m_halvings.front();
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, loadShader(u":/shaders/post.vert.qsb"_s, lcRenderer())},
                               {QRhiShaderStage::Fragment, loadShader(fragmentShader, lcRenderer())}});
    // No vertex buffer: the shader makes the triangle from the vertex index.
    pipeline->setVertexInputLayout({});
    pipeline->setShaderResourceBindings(layout.bindings.get());
    pipeline->setRenderPassDescriptor(m_renderPass.get());
    if (!pipeline->create()) {
        return nullptr;
    }
    return pipeline;
}

bool QcBloomChain::createPipelines(QRhi *rhi)
{
    m_prefilter = createPipeline(rhi, u":/shaders/bloom_prefilter.frag.qsb"_s, 1);
    m_down = createPipeline(rhi, u":/shaders/bloom_down.frag.qsb"_s, 1);
    if (!m_doublings.empty()) {
        m_up = createPipeline(rhi, u":/shaders/bloom_up.frag.qsb"_s, 2);
    }
    if (!m_prefilter || !m_down || (!m_doublings.empty() && !m_up)) {
        return false;
    }

    for (std::size_t level = 0; level < m_halvings.size(); ++level) {
        m_halvings[level].pipeline = level == 0 ? m_prefilter.get() : m_down.get();
    }
    for (Pass &pass : m_doublings) {
        pass.pipeline = m_up.get();
    }
    return true;
}

void QcBloomChain::draw(QRhiCommandBuffer *commandBuffer) const
{
    if (!result()) {
        return;
    }

    const auto drawPass = [commandBuffer](const Pass &pass) {
        const QSize size = pass.texture->pixelSize();
        commandBuffer->beginPass(pass.target.get(), Qt::black, {1.0f, 0});
        commandBuffer->setGraphicsPipeline(pass.pipeline);
        commandBuffer->setViewport(QRhiViewport(0, 0, static_cast<float>(size.width()), static_cast<float>(size.height())));
        commandBuffer->setShaderResources(pass.bindings.get());
        commandBuffer->draw(3);
        commandBuffer->endPass();
    };
    for (const Pass &pass : m_halvings) {
        drawPass(pass);
    }
    // The smallest doubling first: each reads the one below it.
    for (const Pass &pass : std::ranges::reverse_view(m_doublings)) {
        drawPass(pass);
    }
}

QRhiTexture *QcBloomChain::result() const
{
    if (!m_prefilter) {
        return nullptr;
    }
    return m_doublings.empty() ? m_halvings.front().texture.get() : m_doublings.front().texture.get();
}

void QcBloomChain::reset()
{
    releaseResources();
    m_built.reset();
}

void QcBloomChain::releaseResources()
{
    // Bindings and targets before the textures, pipelines and buffers they refer to.
    m_doublings.clear();
    m_halvings.clear();
    m_up.reset();
    m_down.reset();
    m_prefilter.reset();
    m_renderPass.reset();
    m_sampler.reset();
    m_uniforms.reset();
}
