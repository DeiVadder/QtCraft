#pragma once

#include <QMatrix4x4>
#include <QSize>
#include <rhi/qrhi.h>

#include <memory>
#include <optional>

// The screen-space ambient occlusion of the realistic mode (decision E-902): from the scene's depth,
// how much of the hemisphere above each pixel's surface the nearby geometry hides, at half the
// scene's size. The composite pass blurs it and darkens the scene with it. It adds to the smooth
// lighting of the meshes (their corners are already shaded per vertex) what that cannot see: the
// ground under creatures and figures, leaves and plants over other blocks, overhangs and the
// steps of shapes that are not full blocks. Render thread only; owns its GPU resources.
class QcAmbientOcclusion
{
public:
    // The hemisphere's radius in blocks, how dark a fully hidden pixel gets (1 - strength), and the
    // distances over which it fades out (far pixels are smaller than the radius' noise) and below
    // which it is left out (the hand drawn in front of the world).
    static constexpr float radius = 1.0f;
    static constexpr float strength = 0.75f;
    static constexpr float fadeStart = 40.0f;
    static constexpr float fadeEnd = 80.0f;
    static constexpr float nearest = 0.5f;

    // Builds the pass over `depth` (the scene's depth texture) at half its size; nothing to do when
    // it was built for the same texture and size. `samples` per pixel may change from frame to frame.
    // False when the GPU refused a part (the composite then draws without it); the same request is
    // not tried again until reset(). Its uniforms come with upload(), every frame before draw().
    bool prepare(QRhi *rhi, QRhiTexture *depth, int samples);
    // The uniforms of the frame: the camera's view (projection x rotation, decision E-883).
    void upload(QRhiResourceUpdateBatch *updates, const QMatrix4x4 &viewProjection) const;
    // The occlusion pass, a render pass of its own; outside any other pass.
    void draw(QRhiCommandBuffer *commandBuffer) const;
    // The occlusion (1 open, less where hidden) in the red channel, null before prepare() succeeded.
    [[nodiscard]] QRhiTexture *result() const { return m_pipeline ? m_texture.get() : nullptr; }
    // Counts the passes built so far: a new value means a new result texture.
    [[nodiscard]] quint64 generation() const { return m_generation; }
    // Drops everything (a new QRhi, a rebuilt scene).
    void reset();

private:
    // What the pass is built for.
    struct Key
    {
        QRhiTexture *depth = nullptr;
        QSize size;
        bool operator==(const Key &) const = default;
    };

    void releaseResources();
    bool create(QRhi *rhi, QRhiTexture *depth);
    // The half-sized target, in R8 where the GPU renders to it, else RGBA8.
    bool createTarget(QRhi *rhi, QSize size);

    quint64 m_generation = 0;
    int m_samples = 0;
    // Clip depth 0..1 (else -1..1), and whether texture coordinates grow with the clip y: the
    // framebuffer's rows run the way the clip y does (both up, or both down as on Vulkan).
    bool m_depthZeroToOne = true;
    bool m_uvMatchesNdcY = false;
    // The arguments the pass was built (or refused) for; unset after reset().
    // NOLINT(qc-member-access): a GPU resource of the pass, made by prepare() and released only as a group (releaseResources()), in that order.
    std::optional<Key> m_built;
    // NOLINT(qc-member-access): a GPU resource of the pass, made by prepare() and released only as a group (releaseResources()), in that order.
    std::unique_ptr<QRhiBuffer> m_uniforms;
    // NOLINT(qc-member-access): a GPU resource of the pass, made by prepare() and released only as a group (releaseResources()), in that order.
    std::unique_ptr<QRhiSampler> m_sampler;
    // NOLINT(qc-member-access): a GPU resource of the pass, made by prepare() and released only as a group (releaseResources()), in that order.
    std::unique_ptr<QRhiTexture> m_texture;
    // NOLINT(qc-member-access): a GPU resource of the pass, made by prepare() and released only as a group (releaseResources()), in that order.
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPass;
    // NOLINT(qc-member-access): a GPU resource of the pass, made by prepare() and released only as a group (releaseResources()), in that order.
    std::unique_ptr<QRhiTextureRenderTarget> m_target;
    // NOLINT(qc-member-access): a GPU resource of the pass, made by prepare() and released only as a group (releaseResources()), in that order.
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    // NOLINT(qc-member-access): a GPU resource of the pass, made by prepare() and released only as a group (releaseResources()), in that order.
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
};
