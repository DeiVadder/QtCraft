#pragma once

#include "app/rendering/QcDebugLineRenderer.h"
#include "app/rendering/QcGpuMeshStore.h"
#include "app/rendering/QcPrecipitationRenderer.h"
#include "app/rendering/QcSkyRenderer.h"
#include "common/core/QcAabb.h"

#include <QImage>
#include <QList>
#include <rhi/qrhi.h>

#include <memory>
#include <optional>

// The GPU resources of QcWorldRenderer, in two groups by lifetime (decision E-710). Each group is
// created and dropped as a unit; the renderer drops the pass group whenever it drops the device
// group, and first, because the pipelines refer to the device's bindings. A group is dropped by
// handing it out whole (std::exchange) so that its destructor releases it: members go in reverse
// declaration order, and each group declares what refers to other members after them (bindings
// after buffers, textures and samplers). An assignment would release member by member in
// declaration order instead. Render thread only.

// What belongs to one QRhi and survives a change of render pass (the window moved to another
// screen, a resize, a new sample count): the frame uniform buffer, the atlas texture with its
// sampler and bindings, the outline's bindings and vertices, and the entity and hand buffers.
// Dropped (`= {}`) only for a new QRhi. The sections' buffers are in QcGpuMeshStore, which the
// renderer resets with this group but keeps: its queue of received meshes outlives the QRhi.
struct QcWorldDeviceResources
{
    // Most boxes of a target's shape the outline shows.
    static constexpr int maxOutlineBoxes = 16;

    // The frame uniform buffer, the atlas sampler and the outline's buffer and bindings for `rhi`;
    // the atlas texture follows with ensureAtlas().
    [[nodiscard]] static QcWorldDeviceResources create(QRhi *rhi);

    // Whether the atlas of `version` (QcWorldView::FrameInput::atlasVersion) still has to be tried.
    // Tried once per version: a texture the GPU refused is not asked for again every frame.
    [[nodiscard]] bool atlasOutdated(quint64 version) const { return !atlasTried || uploadedAtlasVersion != version; }
    // Uploads `levels` as a new texture, or a magenta placeholder without levels. Drops the bindings
    // that refer to the old texture before the texture; the pipelines that use them are the pass
    // group's and go first (QcWorldPassResources::releaseAtlasPipelines).
    void replaceAtlas(const QList<QImage> &levels, quint64 version, QRhiResourceUpdateBatch *updates);
    // The bindings of the frame uniforms (0) and the atlas (1), created on first use after a new
    // atlas texture; the block, entity and hand pipelines are built with them.
    QRhiShaderResourceBindings *atlasBindings();
    // The line vertices of the first maxOutlineBoxes boxes of `selection`, when it is not empty and
    // differs from the uploaded one.
    void uploadOutline(QRhiResourceUpdateBatch *updates, const QList<QcAabb> &selection);

    QRhi *rhi = nullptr;
    std::unique_ptr<QRhiBuffer> frameUniforms;
    std::unique_ptr<QRhiTexture> atlasTexture;
    std::unique_ptr<QRhiSampler> atlasSampler;
    std::unique_ptr<QRhiBuffer> outlineVertices;
    // Per-frame entity meshes (dynamic buffers that grow on demand, QcGpuBuffers::uploadDynamic).
    QcGpuBuffers entityBuffers;
    QcGpuBuffers handBuffers;
    // The bindings refer to the resources above and are declared after them (released first).
    // Frame uniforms and atlas (atlasBindings()); null until first use after a new atlas texture.
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
    // Frame uniforms only, for the outline pipeline.
    std::unique_ptr<QRhiShaderResourceBindings> outlineBindings;
    // The selection whose lines are in outlineVertices; unset before the first upload.
    std::optional<QList<QcAabb>> uploadedSelection;
    quint32 outlineVertexCount = 0;
    // The atlas's grid of cells and the pixels per cell of atlasTexture's largest level (0 without
    // a texture), for the block shader's filtering (decision E-721).
    QSize atlasGrid{1, 1};
    float atlasCellPixels = 0.0f;
    // The atlas version in atlasTexture, and whether that version was tried.
    quint64 uploadedAtlasVersion = 0;
    bool atlasTried = false;
};

// What is built for one render pass: the pipelines and the sub-renderers that own theirs (with
// their small buffers, each declaring its bindings and pipeline after its buffers). Dropped as a unit for a new QRhi and for
// a render target the pipelines do not fit (fits()), so a new pipeline only needs a member here.
struct QcWorldPassResources
{
    // Whether the pipelines were created for `target`'s pass: a compatible pass descriptor and the
    // same sample count (QRhiRenderPassDescriptor::isCompatible is not documented to compare it).
    [[nodiscard]] bool fits(const QRhiRenderTarget *target) const;
    // Whether the block pipelines exist, without which the world is not drawn.
    [[nodiscard]] bool hasBlockPipelines() const { return solidPipeline && cutoutPipeline && translucentPipeline && waterPipeline; }

    // The block, water, entity and hand pipelines for `target`, all with `atlasBindings`
    // (QcWorldDeviceResources::atlasBindings); remembers the pass and sample count they were built
    // for, and that they were tried (a refused pipeline is not built again every frame).
    void createAtlasPipelines(QRhi *rhi, QRhiRenderTarget *target, QRhiShaderResourceBindings *atlasBindings);
    // The outline's line pipeline with `outlineBindings` (QcWorldDeviceResources::outlineBindings);
    // tried once per group.
    void createOutlinePipeline(QRhi *rhi, QRhiRenderTarget *target, QRhiShaderResourceBindings *outlineBindings);
    // Drops the pipelines built with the atlas bindings, for a new atlas texture, and lets them be
    // tried again; the outline and the sub-renderers do not read the atlas and stay.
    void releaseAtlasPipelines();

    // The opaque pass: solid faces without alpha test, cut-out faces (leaves, plants) with it
    // (decision E-756).
    std::unique_ptr<QRhiGraphicsPipeline> solidPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> cutoutPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> translucentPipeline;
    // The water lists; today the same shaders and blending as translucentPipeline.
    std::unique_ptr<QRhiGraphicsPipeline> waterPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> outlinePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> entityPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> handPipeline;
    QcSkyRenderer skyRenderer;
    QcDebugLineRenderer debugLineRenderer;
    // Rain and snow, one instance per particle (decisions E-395, E-397).
    QcPrecipitationRenderer precipitationRenderer;
    // The pass and sample count the block and entity pipelines were created for.
    std::unique_ptr<QRhiRenderPassDescriptor> renderPass;
    int sampleCount = 0;
    // Whether createAtlasPipelines() and createOutlinePipeline() ran for this group.
    bool atlasPipelinesTried = false;
    bool outlinePipelineTried = false;
};
