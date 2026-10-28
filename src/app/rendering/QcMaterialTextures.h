#pragma once

#include <QList>
#include <rhi/qrhi.h>

#include <memory>
#include <optional>

class QcMaterialAtlas;

// The material maps of the realistic mode on the GPU (decision E-908): the normal and the specular
// atlas (QcMaterialAtlas, mipmapped RGBA8 like the colour atlas) the shadow receivers read at
// bindings 5 and 6. While the realistic mode draws without maps (quality Low, no maps built yet,
// the GPU refused them) the bindings hold two flat 1 x 1 textures instead, so the receivers' layout
// never changes. The classic mode holds nothing (release()). Render thread only.
class QcMaterialTextures
{
public:
    // The bindings of the receivers' fragment shaders.
    static constexpr int normalBinding = 5;
    static constexpr int specularBinding = 6;

    // Whether prepare() with these arguments would make other textures: the bindings that refer
    // to the current ones (QcShadowMap::receiverBindings and the pipelines built with them) must be
    // released before.
    [[nodiscard]] bool replaces(QRhi *rhi, const std::shared_ptr<const QcMaterialAtlas> &maps, bool wanted) const;
    // The flat textures and, when `wanted` and `maps` is set, the maps uploaded into their own
    // batch that goes into `updates` only when every texture was made. A refused upload (QRhi and
    // maps) is remembered and not tried again; the flat textures stand in. False only when even the
    // flat textures were refused (then there are no entries).
    bool prepare(QRhi *rhi, const std::shared_ptr<const QcMaterialAtlas> &maps, bool wanted, QRhiResourceUpdateBatch *updates);
    // False when the GPU refused even the flat textures of `rhi` (remembered): the realistic mode
    // then draws without the receivers, whose shaders read the maps.
    [[nodiscard]] bool usable(QRhi *rhi) const;
    // The receivers' entries at normalBinding and specularBinding; empty before prepare() succeeded.
    [[nodiscard]] QList<QRhiShaderResourceBinding> entries() const;
    // Whether real maps are bound (not the flat textures).
    [[nodiscard]] bool hasMaps() const { return m_gpu.maps != nullptr; }
    // Whether any GPU resource is held (none in the classic mode).
    [[nodiscard]] bool holdsResources() const { return m_gpu.flatNormal || m_gpu.normal; }
    // Bytes of the uploaded maps (0 with the flat textures).
    [[nodiscard]] qint64 bytes() const { return m_gpu.bytes; }
    // Drops the GPU resources (the classic mode draws), remembering a refused upload.
    void release();
    // Drops everything, also the refused upload (a new QRhi).
    void reset();

private:
    struct Request
    {
        QRhi *rhi = nullptr;
        // QcMaterialAtlas::serial of the refused maps, 0 for the flat textures.
        quint64 maps = 0;
        bool operator==(const Request &) const = default;
    };
    // The GPU resources, released as a whole.
    struct Resources
    {
        QRhi *rhi = nullptr;
        // The maps uploaded, null with the flat textures; kept alive while they are on the GPU.
        std::shared_ptr<const QcMaterialAtlas> maps;
        qint64 bytes = 0;
        std::unique_ptr<QRhiSampler> sampler;
        std::unique_ptr<QRhiSampler> specularSampler;
        std::unique_ptr<QRhiTexture> flatNormal;
        std::unique_ptr<QRhiTexture> flatSpecular;
        std::unique_ptr<QRhiTexture> normal;
        std::unique_ptr<QRhiTexture> specular;
    };

    // The maps prepare() would upload: none when not wanted, not set or refused before.
    [[nodiscard]] const QcMaterialAtlas *target(QRhi *rhi, const std::shared_ptr<const QcMaterialAtlas> &maps, bool wanted) const;
    bool createFlat(QRhi *rhi, QRhiResourceUpdateBatch *updates);
    bool upload(const std::shared_ptr<const QcMaterialAtlas> &maps, QRhiResourceUpdateBatch *updates);
    void dropMaps();
    [[nodiscard]] const std::optional<Request> &refused() const { return m_refused; }
    void setRefused(const std::optional<Request> &request) { m_refused = request; }

    // NOLINT(qc-member-access): made and changed only inside prepare(), released as a whole by release().
    Resources m_gpu;
    std::optional<Request> m_refused;
};
