#pragma once

#include "QcCamera.h"
#include "QcAabb.h"
#include "QcBlockPos.h"
#include "QcChunkMesh.h"
#include "QcEntityMesh.h"
#include "QcFramePlan.h"
#include "QcRenderSample.h"
#include "QcDebugLineRenderer.h"
#include "QcPrecipitationRenderer.h"
#include "QcSectionLightPool.h"
#include "QcSkyRenderer.h"

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
#include <unordered_map>

// Render-thread side of QcWorldView. Owns all GPU resources. A frame is the list of steps of the
// render mode (QcFramePlan, decision E-390), drawn in order into their targets.
class QcWorldRenderer : public QQuickRhiItemRenderer
{
public:
    QcWorldRenderer() = default;

protected:
    void initialize(QRhiCommandBuffer *commandBuffer) override;
    void synchronize(QQuickRhiItem *item) override;
    void render(QRhiCommandBuffer *commandBuffer) override;

private:
    struct GpuBuffers
    {
        std::unique_ptr<QRhiBuffer> vertices;
        std::unique_ptr<QRhiBuffer> indices;
        quint32 indexCount = 0;
        QRhiCommandBuffer::IndexFormat indexFormat = QRhiCommandBuffer::IndexUInt32;
        quint64 bytes() const { return (vertices ? vertices->size() : 0) + (indices ? indices->size() : 0); }
    };

    struct GpuMesh
    {
        GpuBuffers opaque;
        GpuBuffers translucent;
        GpuBuffers water;
        // Faces towards the void below the world, drawn only from below y = 0 (QcChunkMesh).
        GpuBuffers underside;
        // Water and other translucent faces in the mesher's order (QcChunkMesh::translucentRuns).
        QList<quint32> translucentRuns;
        QVector3D boundsMin;
        QVector3D boundsMax;
        // GPU bytes of all four lists (section buffer statistics).
        quint64 bytes() const { return opaque.bytes() + translucent.bytes() + water.bytes() + underside.bytes(); }
        QVector3D centre() const { return (boundsMin + boundsMax) * 0.5f; }
    };

    // What the steps of one frame share: the sections in view and where the camera is.
    struct FrameView
    {
        QList<const GpuMesh *> visible;
        // The underside of the world faces down from y = 0: from above it is always turned away.
        bool belowWorld = false;
    };

    void createPipelines();
    void ensureBindings();
    std::unique_ptr<QRhiGraphicsPipeline> createBlockPipeline(const QShader &vertexShader, const QShader &fragmentShader,
                                                              const QRhiVertexInputLayout &inputLayout, bool translucent);
    void createOutlinePipeline();
    void uploadOutline(QRhiResourceUpdateBatch *updates);
    void uploadEntityMesh(QRhiResourceUpdateBatch *updates, const QcEntityMesh *mesh, GpuBuffers &buffers);
    void ensureAtlasTexture(QRhiResourceUpdateBatch *updates);
    void uploadPendingMeshes(QRhiResourceUpdateBatch *updates);
    // The light of a received section into the pool, when the plan's steps read it.
    void uploadSectionLight(QRhiResourceUpdateBatch *updates, quint64 key, const QcChunkMesh *mesh);
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
    void drawBuffers(QRhiCommandBuffer *commandBuffer, const GpuBuffers &buffers, quint32 firstIndex = 0, quint32 count = 0);
    // The pipeline the translucent step draws the water lists with: the translucent one as long as
    // the water pipeline does the same (m_waterLikeTranslucent), so alternating draws need no switch.
    QRhiGraphicsPipeline *waterPipelineInTranslucent() const;
    // The water and translucent faces of one section in the mesher's order, switching pipelines.
    void drawTranslucentRuns(const GpuMesh &mesh, const std::function<void(QRhiGraphicsPipeline *)> &use,
                             QRhiCommandBuffer *commandBuffer);
    GpuBuffers createBuffers(QRhiResourceUpdateBatch *updates, const QList<QcBlockVertex> &vertices,
                             const QList<quint32> &indices);

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
    GpuBuffers m_entityBuffers;
    GpuBuffers m_handBuffers;
    // Kept to detect render-target changes that make the pipelines incompatible.
    std::unique_ptr<QRhiRenderPassDescriptor> m_pipelineRenderPass;

    QcFramePlan m_plan = QcFramePlan::classic();
    // Per-cell light of the sections, filled only when m_plan.sectionLight is set.
    QcSectionLightPool m_lightPool;
    bool m_warnedTarget = false;

    std::unordered_map<quint64, GpuMesh> m_meshes;
    // The latest mesh of each section changed since the last frame (null: removed). One entry per
    // section: a second upload of a section in the same resource update batch would release the
    // buffers of the first while the batch still refers to them (decision E-413).
    std::unordered_map<quint64, std::shared_ptr<const QcChunkMesh>> m_meshUploads;
    bool m_clearAllMeshes = false;

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
    quint64 m_sectionBytes = 0;
};
