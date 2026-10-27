#pragma once

#include "clientcore/settings/QcRenderOptions.h"
#include "common/core/QcVec3.h"

#include <QMatrix4x4>
#include <QVector3D>

#include <array>
#include <optional>

struct QcSkyState;

// The light that casts the shadows of a frame (decision E-903): the sun by day, the moon (weaker)
// by night, faded out near the horizon and under clouds so that the switch between them is never
// seen.
struct QcShadowLight
{
    // Unit vector towards the light (x east, y up, z south).
    QVector3D toLight{0.0f, 1.0f, 0.0f};
    // How much the shadows darken: 0 none (the classic brightness everywhere), 1 a shadowed surface
    // keeps shadowFloor of its sky light.
    float strength = 0.0f;
    // The colour of the sky light on lit surfaces (warm at sunset) and in the shadows (bluish sky
    // fill), each blended towards white by the strength.
    QVector3D litTint{1.0f, 1.0f, 1.0f};
    QVector3D shadowTint{1.0f, 1.0f, 1.0f};
};

// One cascade of the shadow map: a square box in the light's space around a sphere near the camera.
struct QcShadowCascade
{
    // Positions relative to the camera (decision E-883) into the light's clip space, in the
    // OpenGL convention (x, y, z in -1..1; z -1 towards the light).
    QMatrix4x4 lightViewProjection;
    // Half the box's side and the side of one texel, in blocks.
    float radius = 0.0f;
    float texelSize = 0.0f;
};

// The pure parts of the sun shadows (decision E-903): which light casts them, how far each cascade
// reaches, the light's axes and a box per cascade whose texels stay on a fixed grid of the world
// (texel snapping), so shadow edges do not crawl while the camera moves.
namespace QcShadowCascades {

// Each cascade reaches this share of the next one's radius.
inline constexpr float splitRatio = 1.0f / 3.0f;
// The box's middle lies this share of its radius ahead of the camera: more of the map covers what
// the camera looks at, and the box still reaches behind it.
inline constexpr float forwardShift = 0.25f;
// Blocks beyond a box towards the light that still cast into it (trees and cliffs above the box).
inline constexpr float casterReach = 128.0f;
// Below this height of the light above the horizon (sine of its elevation) there are no shadows; at
// fullHeight they have their full strength.
inline constexpr float lowestHeight = 0.03f;
inline constexpr float fullHeight = 0.2f;
// The moon's shadows have this share of the sun's strength.
inline constexpr float moonShare = 0.4f;
// The colours of the light on lit surfaces with the sun at the horizon, and of the sky fill in the
// shadows.
inline constexpr QVector3D sunsetTint{1.0f, 0.78f, 0.56f};
inline constexpr QVector3D skyFillTint{0.84f, 0.92f, 1.08f};
inline constexpr QVector3D moonTint{0.86f, 0.92f, 1.06f};

// The radius of each of `count` cascades, nearest first, the last one `distance`, in the first
// `count` entries (the others 0); all 0 for a count outside 1..maxShadowCascades or a distance that
// is not positive and finite.
using Radii = std::array<float, QcRenderOptions::maxShadowCascades>;
[[nodiscard]] Radii radii(int count, float distance);

// The light's axes: `right` and `up` span the shadow map, `toLight` points to the light. `right`
// lies in the plane y-x the sun and moon cross (perpendicular to z), so the map turns only about
// that axis as the day goes on. Nothing for a zero, NaN or infinite direction.
struct Basis
{
    QVector3D right;
    QVector3D up;
    QVector3D toLight;
};
[[nodiscard]] std::optional<Basis> basis(QVector3D toLight);

// `value` rounded to the nearest multiple of `step` (step > 0).
[[nodiscard]] double snapped(double value, double step);

// The cascade of `radius` for a camera at `camera` (world) looking along `forward`, rendered into
// a tile of `resolution` texels: its middle is moved onto the texel grid of the world, in double
// precision (the camera may be far out, decision E-883).
[[nodiscard]] QcShadowCascade fit(const Basis &axes, const QcVec3 &camera, QVector3D forward, float radius, int resolution);

// Maps the backend's clip space of a shadow pass (after QRhi::clipSpaceCorrMatrix()) to texture
// coordinates of its depth texture: x, y in 0..1 over the tile, z the stored depth. `mirrorY` when
// the texture's v runs against clip y (QRhi::isYUpInFramebuffer() != QRhi::isYUpInNDC(): Metal,
// Direct3D); `depthZeroToOne` when the clip depth runs from 0 to 1.
[[nodiscard]] QMatrix4x4 textureMatrix(bool mirrorY, bool depthZeroToOne);

// The light of the moment `sky`: the sun while it stands above the horizon, else the moon.
[[nodiscard]] QcShadowLight light(const QcSkyState &sky);

} // namespace QcShadowCascades
