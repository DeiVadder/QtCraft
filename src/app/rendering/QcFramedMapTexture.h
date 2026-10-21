#pragma once

#include "clientcore/maps/QcFramedMapAtlas.h"

#include <QList>
#include <rhi/qrhi.h>

#include <memory>

// The framed-map texture on the GPU (decision E-809): QcFramedMapAtlas's tiles in one RGBA8 texture
// without mip levels, sampled nearest like the map in the hand. Only tiles whose version changed
// are uploaded. Its bindings have the layout of the atlas bindings (frame uniforms at 0, the texture
// at 1), so the entity pipeline draws the framed maps with them. Part of the device group
// (QcWorldDeviceResources); render thread only.
class QcFramedMapTexture
{
public:
    // Uploads the tiles whose version the texture does not have yet (every drawn tile after the
    // texture was created), creating texture, sampler and bindings with `frameUniforms` on first
    // use; the bytes uploaded.
    quint32 upload(QRhi *rhi, QRhiResourceUpdateBatch *updates, const QList<QcFramedMapAtlas::Tile> &tiles, QRhiBuffer *frameUniforms);
    // Null until the first upload, or when the GPU refused the texture.
    QRhiShaderResourceBindings *bindings() const { return m_bindings.get(); }

private:
    bool create(QRhi *rhi, QRhiBuffer *frameUniforms);

    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiSampler> m_sampler;
    // Refers to the texture and sampler above (released first).
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    // The version of each tile in the texture.
    QList<quint64> m_uploaded;
    bool m_tried = false;
};
