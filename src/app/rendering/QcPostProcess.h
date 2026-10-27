#pragma once

#include "app/rendering/QcBloomChain.h"

#include <QMatrix4x4>
#include <QVector3D>
#include <rhi/qrhi.h>

#include <memory>
#include <optional>

// The post-processing of the realistic mode (decision E-899): the scene target the world is drawn
// into (an HDR colour texture, RGBA16F where the GPU renders to it, and a depth texture the
// composite reads), the bloom chain over it, and the composite pass into the output: height fog,
// bloom, exposure and tonemapping. Render thread only; owns its GPU resources.
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
    };

    // The scene target of `size`, its bloom chain with `bloomLevels` halvings (0: none; none either
    // without an HDR format) and the composite pipeline for `output`; textures are made again only
    // when the size changes. Uploads go into `updates` only when everything was made. False when
    // the GPU refused a part: everything is released, and the same request (QRhi, size, output pass
    // and sample count) is refused again without a new try until reset().
    bool prepare(QRhi *rhi, QSize size, int bloomLevels, QRhiRenderTarget *output, QRhiResourceUpdateBatch *updates);
    // What the steps before post-processing draw into; null before prepare() succeeded.
    [[nodiscard]] QRhiRenderTarget *sceneTarget() const { return m_gpu.sceneTarget.get(); }
    // Whether the composite adds a bloom.
    [[nodiscard]] bool blooms() const { return m_gpu.boundGeneration != 0; }
    // Whether any GPU resource is held (none in the classic mode).
    [[nodiscard]] bool holdsResources() const { return m_gpu.uniforms || m_gpu.sceneColour || m_gpu.bloom.result(); }
    void uploadComposite(QRhiResourceUpdateBatch *updates, const Composite &composite) const;
    // The bloom chain's passes; after the scene's pass and before the output's.
    void drawBloom(QRhiCommandBuffer *commandBuffer) const { m_gpu.bloom.draw(commandBuffer); }
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
        int outputSampleCount = 0;
        std::unique_ptr<QRhiBuffer> uniforms;
        std::unique_ptr<QRhiSampler> linear;
        std::unique_ptr<QRhiSampler> nearest;
        // Black, bound as the bloom when there is none.
        std::unique_ptr<QRhiTexture> noBloom;
        std::unique_ptr<QRhiTexture> sceneColour;
        std::unique_ptr<QRhiTexture> sceneDepth;
        std::unique_ptr<QRhiRenderPassDescriptor> scenePass;
        std::unique_ptr<QRhiTextureRenderTarget> sceneTarget;
        QcBloomChain bloom;
        // The output pass the composite pipeline was built for.
        std::unique_ptr<QRhiRenderPassDescriptor> outputPass;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
        std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    };

    // Everything prepare() needs, its uploads into `own`.
    bool build(QRhi *rhi, QSize size, int bloomLevels, QRhiRenderTarget *output, QRhiResourceUpdateBatch *own);
    // Uniform buffer, samplers and the black texture.
    bool createShared(QRhi *rhi, QRhiResourceUpdateBatch *own);
    bool createScene(QRhi *rhi, QSize size);
    // The scene target in `colour` with a depth texture of `depth`.
    bool createSceneTarget(QRhi *rhi, QSize size, QRhiTexture::Format colour, QRhiTexture::Format depth);
    // The composite's bindings for the current scene and `bloom` (null: the black texture).
    bool bindComposite(QRhi *rhi, QRhiTexture *bloom);
    bool createCompositePipeline(QRhi *rhi, QRhiRenderTarget *output);

    // NOLINT(qc-member-access): made and changed only inside prepare(), released as a whole by release().
    Resources m_gpu;
    std::optional<Request> m_refused;
};
