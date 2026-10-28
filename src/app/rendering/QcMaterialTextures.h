#pragma once

#include "app/rendering/QcTextureStream.h"

#include <QList>
#include <rhi/qrhi.h>

#include <memory>
#include <optional>
#include <utility>

class QcMaterialAtlas;

// The material maps of the realistic mode on the GPU (decision E-908): the normal and the specular
// atlas (QcMaterialAtlas, mipmapped RGBA8 like the colour atlas) the shadow receivers read at
// bindings 5 and 6. While the realistic mode draws without maps (quality Low, no maps built yet,
// the GPU refused them) the bindings hold two flat 1 x 1 textures instead, so the receivers' layout
// never changes. The classic mode holds nothing (standBy()). The pixels of a set are kept only until
// they are on the GPU or cannot go there (decision E-912): a new renderer or device reports the set
// lost (takeLost), and the game builds it again. Maps whose cells differ from the colour atlas's
// (setAtlasCell) are not bound. Render thread only.
class QcMaterialTextures
{
public:
    // The bindings of the receivers' fragment shaders.
    static constexpr int normalBinding = 5;
    static constexpr int specularBinding = 6;
    // Bytes of the maps uploaded per frame (decision E-912): the staging copies stay a few MiB,
    // and the maps of a 64x pack are on the GPU after about 15 frames.
    static constexpr qint64 uploadBytesPerFrame = qint64{4} * 1024 * 1024;

    // The bytes uploaded per frame, uploadBytesPerFrame unless set (tests stream small maps over
    // many frames).
    [[nodiscard]] qint64 uploadBudget() const { return m_uploadBudget; }
    void setUploadBudget(qint64 bytes) { m_uploadBudget = bytes; }

    // The current set of maps by its QcMaterialAtlas::serial (0: none) and, when the world view
    // hands them over, its pixels (null: the set is the one offered before).
    void offer(quint64 serial, std::shared_ptr<const QcMaterialAtlas> pixels);
    // Pixels per cell of the colour atlas drawn with the maps (QcWorldDeviceResources::atlasCellPixels;
    // 0: not filtered, any maps fit). The block shader takes the maps' level from it (block.frag,
    // surfaceAt), so maps with other cells (the old pack's under a new atlas while the new maps
    // stream) would mix neighbouring tiles: they are not bound, the flat textures stand in.
    void setAtlasCell(int pixels) { m_atlasCell = pixels; }
    // Whether prepare() with these arguments would make other textures: the bindings that refer
    // to the current ones (QcShadowMap::receiverBindings and the pipelines built with them) must be
    // released before.
    [[nodiscard]] bool replaces(QRhi *rhi, bool wanted) const;
    // The flat textures and, when `wanted`, the current maps, uploaded with `updates` at most
    // uploadBudget() per call (QcTextureStream) while the textures bound before stay; the
    // call after the last rows binds them and lets their pixels go. Called once per frame. A
    // refused set (QRhi and set) is remembered and not tried again; the flat textures stand in.
    // False only when even the flat textures were refused (then there are no entries).
    bool prepare(QRhi *rhi, bool wanted, QRhiResourceUpdateBatch *updates);
    // True once per set when the current maps are wanted (and the realistic plan is drawn) but
    // neither on the GPU nor here to upload (a new renderer or device after their pixels went, a
    // stand-by while they were on their way) and not refused; the flat textures stand in meanwhile.
    [[nodiscard]] bool takeLost(bool wanted);
    // The frame is drawn without the realistic plan, so prepare() is not called: a set on its way
    // to the GPU and the pixels held for it go (nothing would upload them; the set is reported lost
    // once it is wanted again). With `keepTextures` (the realistic mode is wanted, the classic plan
    // is only its fallback, e.g. for an empty or refused size) the bound textures stay, so its maps
    // need no rebuild; else nothing stays on the GPU (release()).
    void standBy(bool keepTextures);
    // Whether pixels of a set are held, waiting for their upload.
    [[nodiscard]] bool holdsPixels() const { return pixels() != nullptr; }
    // False when the GPU refused even the flat textures of `rhi` (remembered): the realistic mode
    // then draws without the receivers, whose shaders read the maps.
    [[nodiscard]] bool usable(QRhi *rhi) const;
    // The receivers' entries at normalBinding and specularBinding; empty before prepare() succeeded.
    [[nodiscard]] QList<QRhiShaderResourceBinding> entries() const;
    // Whether real maps are bound (not the flat textures).
    [[nodiscard]] bool hasMaps() const { return m_gpu.normal != nullptr; }
    // Whether any GPU resource is held (none in the classic mode).
    [[nodiscard]] bool holdsResources() const { return m_gpu.flatNormal || m_gpu.normal || m_gpu.uploading != 0; }
    // Bytes of the uploaded maps with all their levels (0 with the flat textures).
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
        // The QcMaterialAtlas::serial of the maps uploaded, 0 with the flat textures.
        quint64 serial = 0;
        qint64 bytes = 0;
        std::unique_ptr<QRhiSampler> sampler;
        std::unique_ptr<QRhiSampler> specularSampler;
        std::unique_ptr<QRhiTexture> flatNormal;
        std::unique_ptr<QRhiTexture> flatSpecular;
        std::unique_ptr<QRhiTexture> normal;
        std::unique_ptr<QRhiTexture> specular;
        // Pixels per cell of the bound maps.
        int cell = 0;
        // The set being uploaded (0: none), its cells and its two textures on their way.
        quint64 uploading = 0;
        int uploadingCell = 0;
        QcTextureStream normalUpload;
        QcTextureStream specularUpload;
    };

    // The serial of the maps prepare() would have on the GPU: 0 when not wanted, none, refused
    // before or lost.
    [[nodiscard]] quint64 target(QRhi *rhi, bool wanted) const;
    bool createFlat(QRhi *rhi, QRhiResourceUpdateBatch *updates);
    // The steps of an upload (prepare()).
    void startUpload(quint64 serial, QRhiResourceUpdateBatch *updates);
    void streamUpload(QRhiResourceUpdateBatch *updates);
    [[nodiscard]] bool uploaded() const;
    void bindUpload();
    // Whether maps with `cell` pixels per cell may be drawn with the colour atlas (setAtlasCell()).
    [[nodiscard]] bool fits(int cell) const;
    // The GPU cannot take the set `serial` (`why`, for the log): remembered, its pixels go.
    void refuse(quint64 serial, const char *why);
    void dropUpload();
    void dropMaps();
    [[nodiscard]] const std::optional<Request> &refused() const { return m_refused; }
    void setRefused(const std::optional<Request> &request) { m_refused = request; }
    [[nodiscard]] quint64 current() const { return m_current; }
    void setCurrent(quint64 serial) { m_current = serial; }
    [[nodiscard]] const std::shared_ptr<const QcMaterialAtlas> &pixels() const { return m_pixels; }
    void setPixels(std::shared_ptr<const QcMaterialAtlas> pixels) { m_pixels = std::move(pixels); }

    // NOLINT(qc-member-access): made and changed only inside prepare(), released as a whole by release().
    Resources m_gpu;
    std::optional<Request> m_refused;
    // The current set, its pixels until they are uploaded, and the set last reported lost.
    quint64 m_current = 0;
    std::shared_ptr<const QcMaterialAtlas> m_pixels;
    quint64 m_reportedLost = 0;
    int m_atlasCell = 0;
    qint64 m_uploadBudget = uploadBytesPerFrame;
};
