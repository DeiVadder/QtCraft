// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <rhi/qrhi.h>

#include <memory>
#include <optional>
#include <vector>

// The bloom of the realistic mode (decision E-899): the HDR scene halved several times (the first
// halving keeps only what is brighter than the threshold), then doubled back up, each level mixed
// with the blurred one below it (the dual filter). The result has half the scene's size and is
// added by the composite pass. Render thread only; owns its textures, targets and pipelines.
class QcBloomChain
{
public:
    // Linear brightness from which the scene blooms (the brightest lit surface is 1), the soft
    // knee below it, the share of the wider levels in each doubling, and the brightest value a
    // pixel brings (against single pixels flickering).
    static constexpr float threshold = 1.1f;
    static constexpr float knee = 0.3f;
    static constexpr float scatter = 0.7f;
    static constexpr float brightest = 32.0f;

    // Builds the chain over `source` (the scene texture) with `levels` halvings in `format`, at most
    // as many as halve to one pixel; nothing to do when it was built for the same arguments. False
    // when the GPU refused a part (the composite then draws without bloom).
    bool prepare(QRhi *rhi, QRhiTexture *source, int levels, QRhiTexture::Format format, QRhiResourceUpdateBatch *updates);
    // The passes of the chain, each its own render pass; outside any other pass.
    void draw(QRhiCommandBuffer *commandBuffer) const;
    // The bloom to add, null before prepare() succeeded.
    [[nodiscard]] QRhiTexture *result() const;
    // Counts the chains built so far: a new value means a new result texture.
    [[nodiscard]] quint64 generation() const { return m_generation; }
    // Drops everything (a new QRhi, a rebuilt scene texture).
    void reset();

private:
    // What a chain is built for.
    struct Key
    {
        QRhiTexture *source = nullptr;
        QSize size;
        int levels = 0;
        QRhiTexture::Format format = QRhiTexture::RGBA16F;
        bool operator==(const Key &) const = default;
    };
    // One pass: what it draws into and with which pipeline and bindings.
    struct Pass
    {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiTextureRenderTarget> target;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
        QRhiGraphicsPipeline *pipeline = nullptr;
    };

    // Drops the GPU resources, keeping what they were built for.
    void releaseResources();
    bool createTextures(QRhi *rhi, const Key &key);
    bool createPipelines(QRhi *rhi);
    // The bindings of every pass, in drawing order from `source`, and the uniforms of all passes
    // at their offsets in the uniform buffer (uploaded once the whole chain was made).
    [[nodiscard]] std::optional<QByteArray> bindPasses(QRhi *rhi, QRhiTexture *source);
    [[nodiscard]] std::unique_ptr<QRhiGraphicsPipeline> createPipeline(QRhi *rhi, const QString &fragmentShader, int textures) const;

    quint64 m_generation = 0;
    // The arguments the chain was built (or refused) for; unset after reset().
    // NOLINT(qc-member-access): a GPU resource of the chain, made by prepare() and released only as a group (releaseResources()), in that order.
    std::optional<Key> m_built;
    // The uniforms of every pass at its own aligned offset.
    // NOLINT(qc-member-access): a GPU resource of the chain, made by prepare() and released only as a group (releaseResources()), in that order.
    std::unique_ptr<QRhiBuffer> m_uniforms;
    // NOLINT(qc-member-access): a GPU resource of the chain, made by prepare() and released only as a group (releaseResources()), in that order.
    std::unique_ptr<QRhiSampler> m_sampler;
    // NOLINT(qc-member-access): a GPU resource of the chain, made by prepare() and released only as a group (releaseResources()), in that order.
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPass;
    // NOLINT(qc-member-access): a GPU resource of the chain, made by prepare() and released only as a group (releaseResources()), in that order.
    std::unique_ptr<QRhiGraphicsPipeline> m_prefilter;
    // NOLINT(qc-member-access): a GPU resource of the chain, made by prepare() and released only as a group (releaseResources()), in that order.
    std::unique_ptr<QRhiGraphicsPipeline> m_down;
    // NOLINT(qc-member-access): a GPU resource of the chain, made by prepare() and released only as a group (releaseResources()), in that order.
    std::unique_ptr<QRhiGraphicsPipeline> m_up;
    // Halvings first (the largest first), then the doublings (the largest last); declared last, so
    // their bindings go before the pipelines and buffers they refer to.
    // NOLINT(qc-member-access): a GPU resource of the chain, made by prepare() and released only as a group (releaseResources()), in that order.
    std::vector<Pass> m_halvings;
    // NOLINT(qc-member-access): a GPU resource of the chain, made by prepare() and released only as a group (releaseResources()), in that order.
    std::vector<Pass> m_doublings;
};
