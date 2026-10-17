#pragma once

#include "clientcore/debug/QcDebugLines.h"

#include <QList>
#include <rhi/qrhi.h>

#include <memory>

// Draws coloured line segments in one draw call, depth-tested against the world but not writing
// depth, in the overlay step: the debug visualisations (QcDebugLines, decision E-225). Each segment
// becomes a quad three pixels wide (hardware lines are one pixel on Metal, too thin on high
// resolution screens). Render thread only; costs nothing while there are no lines.
class QcDebugLineRenderer
{
public:
    // Drops all GPU resources (new QRhi or incompatible render target).
    void reset();
    // Uploads this frame's lines; `frameUniforms` is the world renderer's Frame block.
    void prepare(QRhi *rhi, QRhiRenderTarget *target, QRhiResourceUpdateBatch *updates, QRhiBuffer *frameUniforms,
                 const std::shared_ptr<const QList<QcDebugLineVertex>> &lines);
    // Returns whether it drew.
    bool draw(QRhiCommandBuffer *commandBuffer) const;

private:
    void createPipeline(QRhi *rhi, QRhiRenderTarget *target, QRhiBuffer *frameUniforms);

    // A corner of a segment's quad.
    struct Corner
    {
        float position[3];
        float other[3];
        float side;
        quint8 rgba[4];
    };

    std::unique_ptr<QRhiBuffer> m_vertices;
    QList<Corner> m_corners;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    quint32 m_vertexCount = 0;
};
