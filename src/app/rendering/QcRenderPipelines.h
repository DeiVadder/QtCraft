#pragma once

#include <QLoggingCategory>
#include <QString>
#include <rhi/qrhi.h>

#include <memory>

// What the world renderer and the character preview share, so the preview draws figures exactly
// like the world: shader loading, the frame uniform block of the shaders and the entity pipeline.
namespace QcRenderPipelines {

// std140 layout of the Frame uniform block in the shaders.
struct FrameUniforms
{
    // Projection x the camera's rotation: the shaders take positions relative to the camera
    // (decision E-883), so cameraPosition, the camera in that space, is zero.
    float viewProjection[16];
    float cameraPosition[4];
    float fogColor[4];
    // x: fog start, y: fog end, z: daylight factor, w: minimum light
    float params[4];
    // x: 1 when clip-space depth runs from 0 to 1 (else -1 to 1); y, z: size of the output in
    // pixels (debug lines are drawn that many pixels wide, decision E-225); w: skyLightFloor.
    float extra[4];
    // The atlas for the block shader's filtering (decision E-721): x, y: its grid of cells
    // (QcAtlasLayout::grid), z: pixels per cell in the uploaded texture's largest level, w: most
    // samples along a slanted footprint (1: plain mip mapping).
    float atlas[4];
    // Animated textures (decision E-878): x: the time in ticks, wrapped where every animation of
    // the atlas starts over (QcTextureAtlas::animationCycleTicks); y, z, w unused.
    float animation[4];
};
static_assert(sizeof(FrameUniforms) == 160);

// Light the shaders never go below (caves without sky light).
inline constexpr float minimumLight = 0.08f;
// More floor light per unit of sky light (decision E-723): in the open at night the floor is
// minimumLight + skyLightFloor, under trees a little less, in caves without sky light it stays at
// minimumLight. In daylight the sky light itself is brighter than the floor.
inline constexpr float skyLightFloor = 0.16f;

// A shader baked by qt_add_shaders (":/shaders/<name>.qsb"); invalid and logged in `category`
// (the renderer that needs it) when missing.
QShader loadShader(const QString &path, const QLoggingCategory &category);

// Entity meshes (QcEntityVertex) with depth test and write, back faces culled, drawn with the
// bindings of a frame uniform buffer (0) and the texture atlas (1); vertex input binding 1 takes
// the mesh's offset from the camera per instance (QcDrawOrigins, decision E-883).
std::unique_ptr<QRhiGraphicsPipeline> createEntityPipeline(QRhi *rhi, QRhiRenderTarget *target, QRhiShaderResourceBindings *bindings,
                                                           const QShader &vertexShader, const QShader &fragmentShader);

} // namespace QcRenderPipelines
