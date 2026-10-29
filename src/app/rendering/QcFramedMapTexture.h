// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/maps/QcFramedMapAtlas.h"

#include <QList>
#include <rhi/qrhi.h>

#include <memory>

// The framed-map texture on the GPU (decision E-809): QcFramedMapAtlas's tiles in one RGBA8 texture
// without mip levels, sampled nearest like the map in the hand. Only tiles whose version changed
// are uploaded. Its bindings have the layout of the atlas bindings (frame uniforms at 0, the texture
// at 1, an animation map at 2: a still placeholder of its own, the entity shaders do not read it),
// so the entity pipeline draws the framed maps with them. A pipeline may only be used with bindings
// of its own layout: on Metal a binding less moves the vertex buffers to other slots, and the draw
// reads the draw origins as vertices (a large triangle in garbage colours, decision E-896). Part of
// the device group (QcWorldDeviceResources); render thread only.
class QcFramedMapTexture
{
public:
    // Uploads the tiles whose version the texture does not have yet (every drawn tile after the
    // texture was created), creating texture, sampler and bindings with `frameUniforms` on first
    // use; the bytes uploaded.
    quint32 upload(QRhi *rhi, QRhiResourceUpdateBatch *updates, const QList<QcFramedMapAtlas::Tile> &tiles, QRhiBuffer *frameUniforms);
    // Null until the first upload, or when the GPU refused the texture.
    QRhiShaderResourceBindings *bindings() const { return m_bindings.get(); }
    // New bindings: those of bindings() and `extra` after them, for a pipeline whose layout has
    // more (the receivers of the sun shadows, decision E-903); null before the first upload or when
    // the GPU refused them. The caller owns them and releases them before this texture.
    [[nodiscard]] std::unique_ptr<QRhiShaderResourceBindings> bindingsWith(QRhi *rhi, const QList<QRhiShaderResourceBinding> &extra) const;

private:
    bool create(QRhi *rhi, QRhiResourceUpdateBatch *updates, QRhiBuffer *frameUniforms);

    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiSampler> m_sampler;
    // The placeholder at binding 2, so the bindings match the atlas bindings' layout.
    std::unique_ptr<QRhiTexture> m_stillAnimation;
    std::unique_ptr<QRhiSampler> m_stillSampler;
    // Refers to the texture and sampler above (released first).
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    // The version of each tile in the texture.
    QList<quint64> m_uploaded;
    bool m_tried = false;
};
