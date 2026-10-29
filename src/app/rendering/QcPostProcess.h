// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/rendering/QcAmbientOcclusion.h"
#include "app/rendering/QcBloomChain.h"
#include "clientcore/settings/QcRenderOptions.h"

#include <QMatrix4x4>
#include <QVector3D>
#include <rhi/qrhi.h>

#include <memory>
#include <optional>
#include <utility>

// The post-processing of the realistic mode (decision E-899): the scene target the world is drawn
// into (an HDR colour texture, RGBA16F where the GPU renders to it, and a depth texture the
// composite reads), the ambient occlusion (decision E-902) and the bloom chain over it, and the
// composite pass into the output: ambient occlusion, height fog, bloom, exposure and tonemapping.
// Render thread only; owns its GPU resources.
class QcPostProcess
{
public:
    // The height fog (in blocks): its base above sea level, how fast it thins above the base (1 /
    // scale height), its density per block at the base, the distance from the camera where it
    // begins, and how much brighter it gets towards the sun.
    static constexpr float fogBaseAboveSea = 6.0f;
    static constexpr float fogFalloff = 1.0f / 10.0f;
    static constexpr float fogDensity = 0.0035f;
    static constexpr float fogStart = 24.0f;
    static constexpr float fogSunGlow = 1.5f;
    // The exposure of the setting 1: the tonemapping keeps values up to its knee, so 1 keeps the
    // classic brightness of shadows and mid tones.
    static constexpr float baseExposure = 1.0f;
    // How much of the bloom the composite adds.
    static constexpr float bloomStrength = 4.0f;
    // What the composite pass of one frame needs.
    struct Composite
    {
        // Projection x the camera's rotation the scene was drawn with (decision E-883).
        QMatrix4x4 viewProjection;
        // The fog colour (display values) and the height fog's density per block at its base
        // (0: none).
        QVector3D fogColor;
        float fogDensity = 0.0f;
        float cameraHeight = 0.0f;
        float fogBase = 0.0f;
        // The farthest distance the height fog is gathered over.
        float fogDistance = 0.0f;
        QVector3D sunDirection;
        // How much brighter the fog is towards the sun.
        float sunGlow = 0.0f;
        float exposure = 1.0f;
        // Under water (decision E-906) the water's colour (display values) takes the place of the
        // height fog, thickening by QcWaterLook::extinction per block.
        bool underwater = false;
        QVector3D waterColour;
    };

    // The scene target of `size`, its bloom chain with the halvings of `options` (none without an
    // HDR format), its ambient occlusion with the samples of `options` and the composite pipeline
    // for `output`; textures are made again only when the size changes. A refused bloom or
    // occlusion leaves the composite without it. Uploads go into `updates` only when everything was made. False when
    // the GPU refused a part: everything is released, and the same request (QRhi, size, output pass
    // and sample count) is refused again without a new try until reset().
    bool prepare(QRhi *rhi, QSize size, QcRenderOptions options, QRhiRenderTarget *output, QRhiResourceUpdateBatch *updates);
    // What the steps before post-processing draw into; null before prepare() succeeded.
    [[nodiscard]] QRhiRenderTarget *sceneTarget() const { return m_gpu.sceneTarget.get(); }
    // The same textures again, keeping what was drawn (the steps after the water's copy of the
    // scene, decision E-906). Its pass differs from sceneTarget()'s only in loading instead of
    // clearing: the pipelines built for the scene draw into it (compatible by the Vulkan
    // specification; QRhi's isCompatible() may still answer false on Vulkan, load against clear).
    [[nodiscard]] QRhiRenderTarget *sceneContinuedTarget() const { return m_gpu.sceneContinued.get(); }
    // The scene's colour and depth textures and the colour's format; null before prepare() succeeded.
    [[nodiscard]] QRhiTexture *sceneColour() const { return m_gpu.sceneColour.get(); }
    [[nodiscard]] QRhiTexture *sceneDepth() const { return m_gpu.sceneDepth.get(); }
    [[nodiscard]] QRhiTexture::Format sceneFormat() const { return m_gpu.sceneFormat; }
    // Changes whenever the scene's textures are made anew (another size or format); 0 without them.
    [[nodiscard]] quint64 sceneGeneration() const { return m_gpu.sceneColour ? m_sceneGeneration : 0; }
    // Whether the depth texture holds the clip depth 0..1 (else -1..1 mapped to 0..1).
    [[nodiscard]] bool depthZeroToOne() const { return m_gpu.depthZeroToOne; }
    // Whether the composite adds a bloom.
    [[nodiscard]] bool blooms() const { return m_gpu.boundGeneration != 0; }
    // Whether the composite darkens with an ambient occlusion.
    [[nodiscard]] bool occludes() const { return m_gpu.boundOcclusion != 0; }
    // Whether any GPU resource is held (none in the classic mode).
    [[nodiscard]] bool holdsResources() const
    {
        return m_gpu.uniforms || m_gpu.sceneColour || m_gpu.bloom.result() || m_gpu.occlusion.result();
    }
    void uploadComposite(QRhiResourceUpdateBatch *updates, const Composite &composite) const;
    // The passes of the ambient occlusion and the bloom chain; after the scene's pass and before the
    // output's.
    void drawEffects(QRhiCommandBuffer *commandBuffer) const
    {
        m_gpu.occlusion.draw(commandBuffer);
        m_gpu.bloom.draw(commandBuffer);
    }
    // Fills the output with the finished picture; inside the output's pass.
    void drawComposite(QRhiCommandBuffer *commandBuffer) const;
    // Drops the GPU resources (the classic mode draws), remembering a refused request.
    void release();
    // Drops everything, also the refused request (a new QRhi).
    void reset();

private:
    // What a refused prepare() was asked for.
    struct Request
    {
        QRhi *rhi = nullptr;
        QSize size;
        const QRhiRenderPassDescriptor *output = nullptr;
        int sampleCount = 0;
        bool operator==(const Request &) const = default;
    };

    // The GPU resources, released as a whole (members in reverse order: bindings and pipeline
    // before what they refer to).
    struct Resources
    {
        QRhiTexture::Format sceneFormat = QRhiTexture::RGBA16F;
        // The depth texture holds the clip depth 0..1 (else -1..1 mapped to 0..1).
        bool depthZeroToOne = true;
        // The bloom generation the composite binds (QcBloomChain::generation(); 0: no bloom).
        quint64 boundGeneration = 0;
        // The occlusion generation the composite binds (QcAmbientOcclusion::generation(); 0: none).
        quint64 boundOcclusion = 0;
        int outputSampleCount = 0;
        std::unique_ptr<QRhiBuffer> uniforms;
        std::unique_ptr<QRhiSampler> linear;
        std::unique_ptr<QRhiSampler> nearest;
        // Black, bound as the bloom when there is none.
        std::unique_ptr<QRhiTexture> noBloom;
        // White, bound as the occlusion when there is none.
        std::unique_ptr<QRhiTexture> noOcclusion;
        std::unique_ptr<QRhiTexture> sceneColour;
        std::unique_ptr<QRhiTexture> sceneDepth;
        std::unique_ptr<QRhiRenderPassDescriptor> scenePass;
        std::unique_ptr<QRhiTextureRenderTarget> sceneTarget;
        // The scene's textures with their contents kept (decision E-906), with a pass of its own
        // that loads instead of clearing.
        std::unique_ptr<QRhiRenderPassDescriptor> continuedPass;
        std::unique_ptr<QRhiTextureRenderTarget> sceneContinued;
        QcBloomChain bloom;
        QcAmbientOcclusion occlusion;
        // The output pass the composite pipeline was built for.
        std::unique_ptr<QRhiRenderPassDescriptor> outputPass;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
        std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    };

    // Everything prepare() needs, its uploads into `own`.
    bool build(QRhi *rhi, QSize size, QcRenderOptions options, QRhiRenderTarget *output, QRhiResourceUpdateBatch *own);
    // The bloom and the occlusion of `options` over the current scene, each one the GPU refuses
    // left out: the generations the composite binds (0: none).
    [[nodiscard]] std::pair<quint64, quint64> prepareEffects(QRhi *rhi, QcRenderOptions options, QRhiResourceUpdateBatch *own);
    // Uniform buffer, samplers and the black texture.
    bool createShared(QRhi *rhi, QRhiResourceUpdateBatch *own);
    bool createScene(QRhi *rhi, QSize size);
    // The scene target in `colour` with a depth texture of `depth`.
    bool createSceneTarget(QRhi *rhi, QSize size, QRhiTexture::Format colour, QRhiTexture::Format depth);
    // The target over the same attachments that keeps their contents (sceneContinuedTarget()).
    bool createContinuedTarget(QRhi *rhi, const QRhiTextureRenderTargetDescription &description);
    // The composite's bindings for the current scene, bloom and occlusion (the black and the white
    // texture where there is none).
    bool bindComposite(QRhi *rhi);
    bool createCompositePipeline(QRhi *rhi, QRhiRenderTarget *output);

    // NOLINT(qc-member-access): made and changed only inside prepare(), released as a whole by release().
    Resources m_gpu;
    std::optional<Request> m_refused;
    // Counts the scene's textures made (sceneGeneration()).
    quint64 m_sceneGeneration = 0;
};
