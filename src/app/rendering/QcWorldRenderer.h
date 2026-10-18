#pragma once

#include "app/rendering/QcDebugLineRenderer.h"
#include "app/rendering/QcFramePlan.h"
#include "app/rendering/QcGpuMeshStore.h"
#include "app/rendering/QcPrecipitationRenderer.h"
#include "app/rendering/QcRenderSample.h"
#include "app/rendering/QcSkyRenderer.h"
#include "clientcore/camera/QcCamera.h"
#include "clientcore/meshing/QcChunkMesh.h"
#include "clientcore/meshing/QcEntityMesh.h"
#include "common/core/QcAabb.h"
#include "common/core/QcBlockPos.h"

#include <QColor>
#include <QImage>
#include <QQuickRhiItemRenderer>
#include <QVector3D>
#include <QVector4D>
#include <rhi/qrhi.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>

// Render-thread side of QcWorldView: pipelines and the drawing of a frame, which is the list of
// steps of the render mode (QcFramePlan, decision E-390), drawn in order into their targets. The
// sections' buffers are in QcGpuMeshStore; the per-frame entity, hand and outline buffers are here.
class QcWorldRenderer : public QQuickRhiItemRenderer
{
public:
    QcWorldRenderer() = default;

protected:
    void initialize(QRhiCommandBuffer *commandBuffer) override;
    void synchronize(QQuickRhiItem *item) override;
    void render(QRhiCommandBuffer *commandBuffer) override;

private:
    // What the steps of one frame share: the sections in view and where the camera is.
    struct FrameView
    {
        QList<const QcGpuMesh *> visible;
        // The underside of the world faces down from y = 0: from above it is always turned away.
        bool belowWorld = false;
    };

    void createPipelines();
    void ensureBindings();
    std::unique_ptr<QRhiGraphicsPipeline> createBlockPipeline(const QShader &vertexShader, const QShader &fragmentShader,
                                                              const QRhiVertexInputLayout &inputLayout, bool translucent);
    void createOutlinePipeline();
    void uploadOutline(QRhiResourceUpdateBatch *updates);
    void uploadEntityMesh(QRhiResourceUpdateBatch *updates, const QcEntityMesh *mesh, QcGpuBuffers &buffers);
    void ensureAtlasTexture(QRhiResourceUpdateBatch *updates);
    // Benchmark: CPU time of the render thread since the last frame and the GPU time.
    void startSample(QRhiCommandBuffer *commandBuffer);
    // Atlas, pipelines and the uploads of this frame.
    void prepareResources(QRhiResourceUpdateBatch *updates);
    void uploadFrameUniforms(QRhiResourceUpdateBatch *updates, const QMatrix4x4 &viewProjection, QSize outputSize);
    // The sections inside the view frustum.
    FrameView cull(const QMatrix4x4 &viewProjection) const;
    // The steps of the plan, one GPU render pass per target.
    void drawPlan(QRhiCommandBuffer *commandBuffer, QRhiResourceUpdateBatch *updates, const FrameView &view, QSize outputSize);
    void drawPass(QRhiCommandBuffer *commandBuffer, QcRenderPass pass, const FrameView &view);
    void drawOpaque(QRhiCommandBuffer *commandBuffer, const FrameView &view);
    void drawEntities(QRhiCommandBuffer *commandBuffer);
    // Back to front by section centre; with the plan's water drawn here too, the water of a section
    // comes right before its other translucent faces.
    void drawTranslucent(QRhiCommandBuffer *commandBuffer, const FrameView &view);
    // The water lists alone, back to front, with the water pipeline (a plan with a Water step).
    void drawWater(QRhiCommandBuffer *commandBuffer, const FrameView &view);
    void drawOutlineAndHand(QRhiCommandBuffer *commandBuffer);
    // `count` indices from `firstIndex`, all of them when `count` is 0.
    void drawBuffers(QRhiCommandBuffer *commandBuffer, const QcGpuBuffers &buffers, quint32 firstIndex = 0, quint32 count = 0);
    // The pipeline the translucent step draws the water lists with: the translucent one as long as
    // the water pipeline does the same (m_waterLikeTranslucent), so alternating draws need no switch.
    QRhiGraphicsPipeline *waterPipelineInTranslucent() const;
    // The water and translucent faces of one section in the mesher's order, switching pipelines.
    void drawTranslucentRuns(const QcGpuMesh &mesh, const std::function<void(QRhiGraphicsPipeline *)> &use,
                             QRhiCommandBuffer *commandBuffer);

    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiBuffer> m_frameUniforms;
    std::unique_ptr<QRhiTexture> m_atlasTexture;
    std::unique_ptr<QRhiSampler> m_atlasSampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_opaquePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_translucentPipeline;
    // The water lists; today the same shaders and blending as m_translucentPipeline.
    std::unique_ptr<QRhiGraphicsPipeline> m_waterPipeline;
    // m_waterPipeline has the shaders, blending and resources of m_translucentPipeline. A water
    // pipeline of its own (stage 45.3) sets this to false where it is created.
    bool m_waterLikeTranslucent = true;
    std::unique_ptr<QRhiShaderResourceBindings> m_outlineBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_outlinePipeline;
    std::unique_ptr<QRhiBuffer> m_outlineVertices;
    QList<QcAabb> m_selection;
    std::optional<QList<QcAabb>> m_uploadedSelection;
    quint32 m_outlineVertexCount = 0;
    std::unique_ptr<QRhiGraphicsPipeline> m_entityPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_handPipeline;
    std::shared_ptr<const QcEntityMesh> m_entityMesh;
    std::shared_ptr<const QcEntityMesh> m_handMesh;
    QcGpuBuffers m_entityBuffers;
    QcGpuBuffers m_handBuffers;
    // Kept to detect render-target changes that make the pipelines incompatible.
    std::unique_ptr<QRhiRenderPassDescriptor> m_pipelineRenderPass;

    QcFramePlan m_plan = QcFramePlan::classic();
    bool m_warnedTarget = false;

    // The sections on the GPU and the meshes queued for them.
    QcGpuMeshStore m_meshStore;

    QcCamera m_camera;
    QList<QImage> m_atlasLevels;
    quint64 m_atlasVersion = 0;
    quint64 m_uploadedAtlasVersion = 0;
    bool m_atlasTried = false;
    QcSkyState m_sky;
    QcSkyRenderer m_skyRenderer;
    QcDebugLineRenderer m_debugLineRenderer;
    std::shared_ptr<const QList<QcDebugLineVertex>> m_debugLines;
    // Rain and snow, one instance per particle (decisions E-395, E-397).
    QcPrecipitationRenderer m_precipitationRenderer;
    std::shared_ptr<const QList<QcPrecipitationDrop>> m_precipitation;
    float m_fogStart = 80.0f;
    float m_fogEnd = 128.0f;

    // Benchmark samples, handed to the view in synchronize() while it collects them.
    bool m_collectSamples = false;
    QList<QcRenderSample> m_samples;
    QcRenderSample m_sample;
    qint64 m_lastThreadCpuNs = -1;
};
