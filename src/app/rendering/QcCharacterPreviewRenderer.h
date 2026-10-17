#pragma once

#include "clientcore/camera/QcCamera.h"
#include "clientcore/meshing/QcEntityMesh.h"

#include <QImage>
#include <QQuickRhiItemRenderer>
#include <rhi/qrhi.h>

#include <memory>

// Render-thread side of QcCharacterPreview: draws the figure's mesh with the world's entity
// pipeline and shaders (QcRenderPipelines) over a transparent background, at full daylight and
// without fog. Owns its GPU resources; the texture atlas it shares with the other previews
// (QcPreviewAtlasCache). Lives as long as the item (QML destroys hidden previews, E-231).
class QcCharacterPreviewRenderer : public QQuickRhiItemRenderer
{
public:
    QcCharacterPreviewRenderer();
    ~QcCharacterPreviewRenderer() override;
    QcCharacterPreviewRenderer(const QcCharacterPreviewRenderer &) = delete;
    QcCharacterPreviewRenderer &operator=(const QcCharacterPreviewRenderer &) = delete;

protected:
    void initialize(QRhiCommandBuffer *commandBuffer) override;
    void synchronize(QQuickRhiItem *item) override;
    void render(QRhiCommandBuffer *commandBuffer) override;

private:
    void releaseResources();
    // Atlas texture, bindings and pipeline, recreated when the atlas size or the target changes.
    void prepare(QRhiResourceUpdateBatch *updates);
    void uploadMesh(QRhiResourceUpdateBatch *updates);
    void uploadUniforms(QRhiResourceUpdateBatch *updates);

    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiBuffer> m_uniforms;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::shared_ptr<QRhiTexture> m_atlasTexture;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    std::unique_ptr<QRhiRenderPassDescriptor> m_pipelineRenderPass;
    // Creating the pipeline failed (logged once); tried again only after the target, the atlas or
    // the QRhi changed.
    bool m_pipelineFailed = false;
    std::unique_ptr<QRhiBuffer> m_vertices;
    std::unique_ptr<QRhiBuffer> m_indices;
    quint32 m_indexCount = 0;

    std::shared_ptr<const QcEntityMesh> m_mesh;
    std::shared_ptr<const QcEntityMesh> m_uploadedMesh;
    QcCamera m_camera;
    QList<QImage> m_atlasLevels;
    quint64 m_atlasVersion = 0;
    quint64 m_uploadedAtlasVersion = 0;
};
