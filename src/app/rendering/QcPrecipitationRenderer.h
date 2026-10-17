#pragma once

#include "clientcore/weather/QcPrecipitationField.h"

#include <QList>
#include <rhi/qrhi.h>

#include <memory>

// Draws the rain and snow (QcPrecipitationField, decisions E-395, E-397) in one instanced draw call,
// depth-tested against the world but not writing depth, after the translucent faces: six static
// corners of a quad, one 16-byte instance per particle (QcPrecipitationDrop) uploaded as it comes
// from the field. precipitation.vert makes a raindrop a thin streak and a snowflake a round dot
// facing the camera; both take the sky's light and the fog. Render thread only; costs nothing
// while no particle falls.
class QcPrecipitationRenderer
{
public:
    // Drops all GPU resources (new QRhi or incompatible render target).
    void reset();
    // Uploads this frame's particles; `frameUniforms` is the world renderer's Frame block.
    void prepare(QRhi *rhi, QRhiRenderTarget *target, QRhiResourceUpdateBatch *updates, QRhiBuffer *frameUniforms,
                 const std::shared_ptr<const QList<QcPrecipitationDrop>> &drops);
    // Returns whether it drew.
    bool draw(QRhiCommandBuffer *commandBuffer) const;

private:
    void createResources(QRhi *rhi, QRhiRenderTarget *target, QRhiResourceUpdateBatch *updates, QRhiBuffer *frameUniforms);

    std::unique_ptr<QRhiBuffer> m_corners;
    std::unique_ptr<QRhiBuffer> m_instances;
    std::unique_ptr<QRhiBuffer> m_shape;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    quint32 m_instanceCount = 0;
};
