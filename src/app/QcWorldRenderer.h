#pragma once

#include "QcCamera.h"
#include "QcBlockPos.h"
#include "QcChunkMesh.h"

#include <QColor>
#include <QImage>
#include <QQuickRhiItemRenderer>
#include <QVector3D>
#include <rhi/qrhi.h>

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
    };

    struct GpuMesh
    {
        GpuBuffers opaque;
        GpuBuffers translucent;
        QVector3D boundsMin;
        QVector3D boundsMax;
    };

    void createPipelines();
    void createOutlinePipeline();
    void uploadOutline(QRhiResourceUpdateBatch *updates);
    void ensureAtlasTexture(QRhiResourceUpdateBatch *updates);
    void uploadPendingMeshes(QRhiResourceUpdateBatch *updates);
    GpuBuffers createBuffers(QRhiResourceUpdateBatch *updates, const QList<QcBlockVertex> &vertices,
                             const QList<quint32> &indices);
    bool isVisible(const GpuMesh &mesh, const QMatrix4x4 &viewProjection) const;

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
    std::optional<QcBlockPos> m_selection;
    std::optional<QcBlockPos> m_uploadedSelection;
    // Kept to detect render-target changes that make the pipelines incompatible.
    std::unique_ptr<QRhiRenderPassDescriptor> m_pipelineRenderPass;

    std::unordered_map<quint64, GpuMesh> m_meshes;
    QList<std::pair<quint64, std::shared_ptr<const QcChunkMesh>>> m_meshUploads;
    bool m_clearAllMeshes = false;

    QcCamera m_camera;
    QImage m_atlasImage;
    quint64 m_atlasVersion = 0;
    quint64 m_uploadedAtlasVersion = 0;
    QColor m_skyColor;
    float m_daylight = 1.0f;
    float m_fogStart = 80.0f;
    float m_fogEnd = 128.0f;
};
