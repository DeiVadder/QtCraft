// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/light/QcWaterLook.h"
#include "clientcore/settings/QcRenderOptions.h"

#include <QList>
#include <rhi/qrhi.h>

#include <memory>
#include <optional>

class QcPostProcess;
class QcShadowMap;
struct QcWorldDeviceResources;

// The water of the realistic mode (decision E-906): a copy of the scene drawn before the water
// (colour and distance from the camera, RGBA16F) that the water looks through, the uniform buffer of
// the water shaders, and the water's pipeline with its bindings (the atlas bindings, the shadow
// receivers' entries when the sun casts shadows, the copy at 5 and the uniforms at 6). In the classic
// mode it holds nothing (release()). Render thread only; owns its GPU resources.
class QcWaterSurface
{
public:
    // The water's pipeline and the bindings to draw it with.
    struct Draw
    {
        QRhiGraphicsPipeline *pipeline = nullptr;
        QRhiShaderResourceBindings *bindings = nullptr;
    };

    // The copy of `post`'s scene (made again when the scene's textures are), its target, the copy's
    // pipeline and the uniform buffer for `water`. False without water (the classic mode) or when the
    // GPU refused a part: everything is released, and a refused request (QRhi, scene) is not tried
    // again until reset(). Nothing is uploaded here (the uniforms are a dynamic buffer).
    bool prepare(QRhi *rhi, const QcPostProcess &post, QcRenderOptions::Water water);
    // Whether the copy step and the water's pipeline can be used: prepare() succeeded.
    [[nodiscard]] bool ready() const { return m_gpu.copyPipeline != nullptr; }
    // Whether any GPU resource is held (none in the classic mode).
    [[nodiscard]] bool holdsResources() const { return m_gpu.uniforms || m_gpu.behind || m_gpu.copyPipeline; }
    // The copy of the scene the water looks through (null before prepare() succeeded).
    [[nodiscard]] QRhiTexture *behind() const { return m_gpu.behind.get(); }

    void upload(QRhiResourceUpdateBatch *updates, const QcWaterUniforms &uniforms) const;
    // The copy step: a render pass of its own into the copy, after the scene's first pass.
    void drawCopy(QRhiCommandBuffer *commandBuffer) const;
    // The water's pipeline for the scene drawn on after the copy (`target`, the scene's continued
    // target), with the device's atlas bindings and, with `shadows` (null or not ready: none), the
    // receivers' entries (QcShadowMap::receiverEntries): made on first use and again when the
    // shadows come, go or get another map (QcShadowMap::generation), the pass changes or the copy
    // is made anew; nothing when the GPU refused it (tried once per such state).
    [[nodiscard]] std::optional<Draw> pipeline(const QcWorldDeviceResources &device, const QcShadowMap *shadows, QRhiRenderTarget *target);
    // Drops what refers to the atlas or the shadow map (a new atlas texture, another shadow map).
    void releaseAtlas();
    // Drops the GPU resources (the classic mode draws), remembering a refused request.
    void release();
    // Drops everything, also the refused request (a new QRhi).
    void reset();

private:
    struct Request
    {
        QRhi *rhi = nullptr;
        quint64 scene = 0;
        bool operator==(const Request &) const = default;
    };

    // The GPU resources, released as a whole (members in reverse order: pipelines and bindings
    // before what they refer to).
    struct Resources
    {
        QRhi *rhi = nullptr;
        // The scene generation (QcPostProcess::sceneGeneration) the copy was made for.
        quint64 scene = 0;
        std::unique_ptr<QRhiBuffer> uniforms;
        std::unique_ptr<QRhiSampler> nearest;
        std::unique_ptr<QRhiSampler> linear;
        std::unique_ptr<QRhiTexture> behind;
        std::unique_ptr<QRhiRenderPassDescriptor> behindPass;
        std::unique_ptr<QRhiTextureRenderTarget> behindTarget;
        std::unique_ptr<QRhiShaderResourceBindings> copyBindings;
        std::unique_ptr<QRhiGraphicsPipeline> copyPipeline;
        // The water's, made on first use (releaseAtlas()).
        std::unique_ptr<QRhiShaderResourceBindings> waterBindings;
        std::unique_ptr<QRhiGraphicsPipeline> waterPipeline;
        // Whether the water's pipeline was tried, with which shadow map (QcShadowMap::generation, 0:
        // none), for which pass.
        bool waterTried = false;
        quint64 waterShadowMap = 0;
        const QRhiRenderPassDescriptor *waterPass = nullptr;
    };

    bool build(QRhi *rhi, const QcPostProcess &post);
    bool createShared(QRhi *rhi);
    // The copy of the scene's size and its pass, and the pipeline that fills it from `post`'s scene.
    bool createCopy(QRhi *rhi, const QcPostProcess &post);
    bool createWater(const QcWorldDeviceResources &device, const QList<QRhiShaderResourceBinding> &shadowEntries, QRhiRenderTarget *target);
    [[nodiscard]] const std::optional<Request> &refused() const { return m_refused; }
    void setRefused(const std::optional<Request> &request) { m_refused = request; }

    // NOLINT(qc-member-access): made and changed only inside prepare() and pipeline(), released as a whole by release().
    Resources m_gpu;
    std::optional<Request> m_refused;
};
