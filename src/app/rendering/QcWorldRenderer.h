#pragma once

#include "QcCamera.h"
#include "QcAabb.h"
#include "QcBlockPos.h"
#include "QcChunkMesh.h"
#include "QcEntityMesh.h"
#include "QcRenderSample.h"
#include "QcDebugLineRenderer.h"
#include "QcSkyRenderer.h"

#include <QColor>
#include <QImage>
#include <QQuickRhiItemRenderer>
#include <QVector3D>
#include <QVector4D>
#include <rhi/qrhi.h>

#include <array>
#include <memory>
#include <optional>
#include <unordered_map>

// Render-thread side of QcWorldView. Owns all GPU resources.
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
        QVector3D boundsMin;
        QVector3D boundsMax;
    };

    void createPipelines();
    void ensureBindings();
    std::unique_ptr<QRhiGraphicsPipeline> createBlockPipeline(const QShader &vertexShader, const QShader &fragmentShader,
                                                              const QRhiVertexInputLayout &inputLayout, bool translucent);
    std::unique_ptr<QRhiGraphicsPipeline> createEntityPipeline(const QShader &vertexShader, const QShader &fragmentShader,
                                                               const QRhiVertexInputLayout &inputLayout);
    void createOutlinePipeline();
    void uploadOutline(QRhiResourceUpdateBatch *updates);
    void uploadEntityMesh(QRhiResourceUpdateBatch *updates, const QcEntityMesh *mesh, GpuBuffers &buffers);
    void ensureAtlasTexture(QRhiResourceUpdateBatch *updates);
    void uploadPendingMeshes(QRhiResourceUpdateBatch *updates);
    // Benchmark: CPU time of the render thread since the last frame and the GPU time.
    void startSample(QRhiCommandBuffer *commandBuffer);
    // Atlas, pipelines and the uploads of this frame.
    void prepareResources(QRhiResourceUpdateBatch *updates);
    void uploadFrameUniforms(QRhiResourceUpdateBatch *updates, const QMatrix4x4 &viewProjection, QSize outputSize);
    // Visible sections, entities, translucent faces, outline and hand, in this order.
    void drawWorld(QRhiCommandBuffer *commandBuffer, const QMatrix4x4 &viewProjection, QSize outputSize);
    void drawTranslucent(QRhiCommandBuffer *commandBuffer, const QList<const GpuMesh *> &visible);
    void drawOutlineAndHand(QRhiCommandBuffer *commandBuffer);
    void drawBuffers(QRhiCommandBuffer *commandBuffer, const GpuBuffers &buffers);
    GpuBuffers createBuffers(QRhiResourceUpdateBatch *updates, const QList<QcBlockVertex> &vertices,
                             const QList<quint32> &indices);

    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiBuffer> m_frameUniforms;
    std::unique_ptr<QRhiTexture> m_atlasTexture;
    std::unique_ptr<QRhiSampler> m_atlasSampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_opaquePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_translucentPipeline;
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

    std::unordered_map<quint64, GpuMesh> m_meshes;
    QList<std::pair<quint64, std::shared_ptr<const QcChunkMesh>>> m_meshUploads;
    bool m_clearAllMeshes = false;

    QcCamera m_camera;
    QImage m_atlasImage;
    quint64 m_atlasVersion = 0;
    quint64 m_uploadedAtlasVersion = 0;
    QcSkyState m_sky;
    QcSkyRenderer m_skyRenderer;
    QcDebugLineRenderer m_debugLineRenderer;
    std::shared_ptr<const QList<QcDebugLineVertex>> m_debugLines;
    float m_fogStart = 80.0f;
    float m_fogEnd = 128.0f;

    // Benchmark samples, handed to the view in synchronize() while it collects them.
    bool m_collectSamples = false;
    QList<QcRenderSample> m_samples;
    QcRenderSample m_sample;
    qint64 m_lastThreadCpuNs = -1;
    quint64 m_sectionBytes = 0;
};
