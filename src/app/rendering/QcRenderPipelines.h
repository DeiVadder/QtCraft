#pragma once

#include <QLoggingCategory>
#include <QString>
#include <rhi/qrhi.h>

#include <memory>

// What the world renderer and the character preview share, so the preview draws figures exactly
// like the world: shader loading, the frame uniform block of the shaders and the entity pipeline.
namespace QcRenderPipelines {

// std140 layout of the Frame uniform block in the shaders (the block shaders read only the first
// four members).
struct FrameUniforms
{
    float viewProjection[16];
    float cameraPosition[4];
    float fogColor[4];
    // x: fog start, y: fog end, z: daylight factor, w: minimum light
    float params[4];
    // x: 1 when clip-space depth runs from 0 to 1 (else -1 to 1); y, z: size of the output in
    // pixels (debug lines are drawn that many pixels wide, decision E-225).
    float extra[4];
};
static_assert(sizeof(FrameUniforms) == 128);

// Light the shaders never go below (deep caves, night).
inline constexpr float minimumLight = 0.08f;

// A shader baked by qt_add_shaders (":/shaders/<name>.qsb"); invalid and logged in `category`
// (the renderer that needs it) when missing.
QShader loadShader(const QString &path, const QLoggingCategory &category);

// Entity meshes (QcEntityVertex) with depth test and write, back faces culled, drawn with the
// bindings of a frame uniform buffer (0) and the texture atlas (1).
std::unique_ptr<QRhiGraphicsPipeline> createEntityPipeline(QRhi *rhi, QRhiRenderTarget *target, QRhiShaderResourceBindings *bindings,
                                                           const QShader &vertexShader, const QShader &fragmentShader);

} // namespace QcRenderPipelines
