#pragma once

#include "clientcore/light/QcSky.h"
#include "common/core/QcVec3.h"

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
        QcVec3 camera;
        // Extra brightness of the sun's disc and halo, so that it blooms in the HDR picture of the
        // realistic mode (decision E-899); 0 draws the classic sun.
        float sunGain = 0.0f;
    };

    // The cloud pattern repeats after this many metres (sky.frag: 1000 cells of 40 m): camera and
    // drift are wrapped by it, so the shader's floats stay small far out (decision E-883).
    static constexpr double cloudPeriod = 40000.0;

    void prepare(QRhi *rhi, QRhiRenderTarget *target, QRhiResourceUpdateBatch *updates, const Frame &frame);
    void draw(QRhiCommandBuffer *commandBuffer) const;

private:
    void createPipeline(QRhi *rhi, QRhiRenderTarget *target);

    std::unique_ptr<QRhiBuffer> m_uniforms;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
};
