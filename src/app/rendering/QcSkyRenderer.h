#pragma once

#include "clientcore/light/QcSky.h"

#include <QMatrix4x4>
#include <QVector3D>
#include <rhi/qrhi.h>

#include <memory>

// Draws the sky dome (gradient, sun, moon with its phase, stars, clouds) as one screen-filling
// triangle before the world. Render thread only; owns its pipeline and uniform buffer.
class QcSkyRenderer
{
public:
    // Drops all GPU resources (new QRhi or incompatible render target).
    void reset();
    // What one frame of the sky is drawn from.
    struct Frame
    {
        // Projection x view without the camera's translation.
        QMatrix4x4 viewRotation;
        QcSkyState sky;
        QVector3D camera;
    };

    void prepare(QRhi *rhi, QRhiRenderTarget *target, QRhiResourceUpdateBatch *updates, const Frame &frame);
    void draw(QRhiCommandBuffer *commandBuffer) const;

private:
    void createPipeline(QRhi *rhi, QRhiRenderTarget *target);

    std::unique_ptr<QRhiBuffer> m_uniforms;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
};
