#pragma once

#include "clientcore/light/QcShadowCascades.h"
#include "clientcore/settings/QcRenderOptions.h"
#include "common/core/QcVec3.h"

#include <QMatrix4x4>
#include <QVector3D>
#include <rhi/qrhi.h>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>

struct QcWorldDeviceResources;

// The sun's shadow map of the realistic mode (decision E-903): one depth texture with the cascades
// of the quality side by side (QcRenderOptions::shadows), the render target the shadow step draws
// into, the uniform buffers of its casters (one section per cascade, at a dynamic offset) and of
// the shaders that receive the shadows, and the bindings and pipelines that also refer to the
// atlas. In the classic mode it holds nothing (release()). Render thread only; owns its GPU
// resources.
class QcShadowMap
{
public:
    // The share of the sky light a surface keeps in a full shadow.
    static constexpr float shadowFloor = 0.6f;
    // The shadows fade out from this share of the shadow distance to its end.
    static constexpr float fadeStart = 0.8f;
    // What one frame's cascades are fitted to.
    struct Frame
    {
        // The camera in the world and where it looks.
        QcVec3 camera;
        QVector3D forward;
        // The camera's offset from the render anchor (QcDrawOrigins::follow), for the casters.
        QVector3D drawCamera;
        QcShadowLight light;
        // The receivers' material block (decision E-908): bound, highlights of the moving lights,
        // relief and highlight strength.
        std::array<float, 4> material{};
    };
    // The pipelines and bindings of the shadow step: solid faces, cut-out faces and entities.
    struct Casters
    {
        QRhiGraphicsPipeline *solid = nullptr;
        QRhiGraphicsPipeline *cutout = nullptr;
        QRhiGraphicsPipeline *entities = nullptr;
        QRhiShaderResourceBindings *bindings = nullptr;
    };

    // Whether prepare() with `shadows` would make a new map or drop it: the pipelines built with
    // receiverBindings() must be released before.
    [[nodiscard]] bool replaces(QRhi *rhi, QcRenderOptions::Shadows shadows) const;
    // The map, its target and buffers for `shadows`; made again only when the cascades or their
    // resolution change. False when there are no cascades or the GPU refused a part: everything is
    // released, and a refused request (QRhi and shadows) is not tried again until reset().
    bool prepare(QRhi *rhi, QcRenderOptions::Shadows shadows);
    // Whether the shadow step can draw: prepare() succeeded.
    [[nodiscard]] bool ready() const { return m_gpu.target != nullptr; }
    // Whether any GPU resource is held (none in the classic mode).
    [[nodiscard]] bool holdsResources() const { return m_gpu.map || m_gpu.receiverUniforms || m_gpu.casterUniforms; }
    [[nodiscard]] QRhiRenderTarget *target() const { return m_gpu.target.get(); }
    [[nodiscard]] int cascadeCount() const { return m_gpu.shadows.cascades; }
    [[nodiscard]] int resolution() const { return m_gpu.shadows.resolution; }
    // The depth format of the map (D16 where the GPU has it).
    [[nodiscard]] QRhiTexture::Format format() const { return m_gpu.format; }
    // Changes whenever the map and its buffers are made anew (another QRhi, cascade count or
    // resolution); 0 without them. Bindings made from receiverEntries() compare it.
    [[nodiscard]] quint64 generation() const { return ready() ? m_generation : 0; }

    // Fits the cascades to `frame` and uploads them for the casters and the receivers.
    void upload(QRhiResourceUpdateBatch *updates, const Frame &frame);
    // Whether the last upload() has shadows at all (a light above the horizon, not overcast): else
    // the receivers do not read the map, and the shadow step need not draw it.
    [[nodiscard]] bool casts() const { return m_casts; }
    void setCasts(bool casts) { m_casts = casts; }
    // The clip matrix of cascade `index` of the last upload() (positions relative to the camera),
    // for culling its casters.
    [[nodiscard]] const QMatrix4x4 &casterMatrix(int index) const;
    // The dynamic offset of cascade `index`'s section in the casters' uniform buffer.
    [[nodiscard]] QRhiCommandBuffer::DynamicOffset casterOffset(int index) const;

    // The bindings of the atlas pipelines with the shadows (QcWorldDeviceResources::atlasBindings
    // plus the receivers' uniform buffer at 3, the map with a comparing sampler at 4 and the
    // `materials` maps at 5 and 6, QcMaterialTextures::entries), made on first use for the device's
    // atlas. Null without an atlas; a refused one releases everything like a refused prepare().
    [[nodiscard]] QRhiShaderResourceBindings *receiverBindings(const QcWorldDeviceResources &device,
                                                               const QList<QRhiShaderResourceBinding> &materials);
    // The receivers' entries of those bindings (the uniform buffer at 3, the map at 4), for other
    // bindings with the receivers' layout (the water's, decision E-906); empty before prepare()
    // succeeded.
    [[nodiscard]] QList<QRhiShaderResourceBinding> receiverEntries() const;
    // The framed maps' bindings (QcFramedMapTexture) with the receivers' layout, so the entity
    // pipeline built with receiverBindings() draws them too (decision E-896); made on first use,
    // null before the framed-map texture exists or when the GPU refused them.
    [[nodiscard]] QRhiShaderResourceBindings *framedMapBindings(const QcWorldDeviceResources &device,
                                                                const QList<QRhiShaderResourceBinding> &materials);
    // The casters' pipelines and bindings for the device's atlas, made on first use; nothing when
    // the GPU refused them (tried once per atlas).
    [[nodiscard]] std::optional<Casters> casters(const QcWorldDeviceResources &device);
    // Drops what refers to the atlas (a new atlas texture); the pipelines built with
    // receiverBindings() are released before.
    void releaseAtlasBindings();
    // Drops the GPU resources (the classic mode draws), remembering a refused request.
    void release();
    // Drops everything, also the refused request (a new QRhi).
    void reset();

private:
    struct Request
    {
        QRhi *rhi = nullptr;
        QcRenderOptions::Shadows shadows;
        bool operator==(const Request &) const = default;
    };

    // The GPU resources, released as a whole (members in reverse order: pipelines and bindings
    // before what they refer to).
    struct Resources
    {
        QRhi *rhi = nullptr;
        QcRenderOptions::Shadows shadows;
        QRhiTexture::Format format = QRhiTexture::D16;
        // The casters' section per cascade, aligned for dynamic offsets.
        quint32 casterStride = 0;
        std::unique_ptr<QRhiBuffer> receiverUniforms;
        std::unique_ptr<QRhiBuffer> casterUniforms;
        std::unique_ptr<QRhiSampler> compare;
        std::unique_ptr<QRhiTexture> map;
        std::unique_ptr<QRhiRenderPassDescriptor> pass;
        std::unique_ptr<QRhiTextureRenderTarget> target;
        // What refers to the atlas, made on first use (releaseAtlasBindings()).
        std::unique_ptr<QRhiShaderResourceBindings> receiverBindings;
        std::unique_ptr<QRhiShaderResourceBindings> framedMapBindings;
        std::unique_ptr<QRhiShaderResourceBindings> casterBindings;
        std::unique_ptr<QRhiGraphicsPipeline> solidCaster;
        std::unique_ptr<QRhiGraphicsPipeline> cutoutCaster;
        std::unique_ptr<QRhiGraphicsPipeline> entityCaster;
        bool castersTried = false;
    };

    bool build(QRhi *rhi, QcRenderOptions::Shadows shadows);
    bool createBuffers(QRhi *rhi, int cascades);
    bool createMap(QRhi *rhi, QcRenderOptions::Shadows shadows);
    bool createCasters(const QcWorldDeviceResources &device);
    [[nodiscard]] const std::optional<Request> &refused() const { return m_refused; }
    void setRefused(const std::optional<Request> &request) { m_refused = request; }
    void setCasterMatrix(int index, const QMatrix4x4 &matrix) { m_casterMatrices.at(static_cast<std::size_t>(index)) = matrix; }

    // NOLINT(qc-member-access): made and changed only inside prepare() and the atlas bindings, released as a whole by release().
    Resources m_gpu;
    std::optional<Request> m_refused;
    std::array<QMatrix4x4, QcRenderOptions::maxShadowCascades> m_casterMatrices{};
    bool m_casts = false;
    // Counts the maps made (generation()).
    quint64 m_generation = 0;
};
