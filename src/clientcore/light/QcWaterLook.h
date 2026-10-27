#pragma once

#include "clientcore/light/QcShadowCascades.h"
#include "clientcore/light/QcSky.h"
#include "clientcore/settings/QcRenderOptions.h"
#include "common/core/QcVec3.h"

#include <QMatrix4x4>
#include <QSize>

#include <array>

class QcBlockAccess;

// The std140 layout of the Water block of water.frag and water_behind.frag (decision E-906).
struct QcWaterUniforms
{
    // Inverse of projection x the camera's rotation: positions relative to the camera (E-883).
    std::array<float, 16> inverseViewProjection{};
    // rgb: the sky's zenith (display values), w: the waves' time in seconds (QcWaterLook::waveLoopSeconds).
    std::array<float, 4> zenith{};
    // rgb: the sky's horizon, w: 1 when the depth buffer holds the clip depth 0..1.
    std::array<float, 4> horizon{};
    // rgb: the glow around the sun near the horizon, w: +1 or -1, the y of texture coordinates per
    // y of normalised device coordinates.
    std::array<float, 4> glow{};
    // xyz: towards the light the water mirrors (the sun by day, the moon by night), w: its strength
    // (0: no highlight, overcast or no light above the horizon).
    std::array<float, 4> toLight{};
    // rgb: the light's colour on lit surfaces, w: unused.
    std::array<float, 4> lightColour{};
    // xyz: the camera's world position modulo QcWaterLook::wavePeriod, w: unused.
    std::array<float, 4> camera{};
    // xy: one pixel of the scene in texture coordinates, z: wave octaves, w: refraction strength.
    std::array<float, 4> pixel{};
    // x: steps of the screen-space reflections (0: none), y: strength of the caustics, zw: unused.
    std::array<float, 4> effects{};
    // rgb: how much of the light the water takes per block (linear), w: unused.
    std::array<float, 4> extinction{};
    // rgb: the colour of the water around a camera under water (underwaterColour), seen where the
    // surface mirrors the water below it; w: unused.
    std::array<float, 4> deep{};
};
static_assert(sizeof(QcWaterUniforms) == 224);

// What one frame's water is drawn with.
struct QcWaterFrame
{
    // Projection x the camera's rotation the scene is drawn with.
    QMatrix4x4 viewProjection;
    QcSkyState sky;
    // The light of the shadows (QcShadowCascades::light): the water mirrors the same sun or moon.
    QcShadowLight light;
    QcVec3 camera;
    // The waves' time (FrameInput::waterSeconds).
    float seconds = 0.0f;
    // The backend's conventions (QRhi::isClipDepthZeroToOne, isYUpInFramebuffer == isYUpInNDC).
    bool depthZeroToOne = true;
    bool textureYFollowsNdc = false;
    // The scene's size in pixels.
    QSize size;
    QcRenderOptions::Water water;
};

// The look of the water in the realistic mode (decision E-906): what does not need the GPU.
namespace QcWaterLook {

// The waves and the caustics repeat every this many blocks, so the shaders take the camera modulo
// it in floats however far out it is (decision E-883).
inline constexpr double wavePeriod = 64.0;
// The waves repeat after this many seconds (their frequencies are whole multiples of it), so their
// time can wrap without a jump.
inline constexpr double waveLoopSeconds = 600.0;
// How much of each colour of light the water takes per block (linear): red goes first, then green,
// so shallow water shows the ground in a light green-blue and deep water turns dark blue.
inline constexpr std::array<float, 3> extinction = {0.30f, 0.09f, 0.06f};
// The colour of the light the water scatters towards the eye in full daylight (display values),
// and the share of it that stays in the darkest night.
inline constexpr std::array<float, 3> scatter = {0.10f, 0.30f, 0.42f};
inline constexpr float nightScatter = 0.08f;

// Whether the eye at `eye` is under water: its cell holds water and the eye is below its surface
// (or water lies on top of it).
[[nodiscard]] bool eyeUnderWater(const QcBlockAccess &world, const QcVec3 &eye);
// The waves' time of the animation clock `ticks` (QcAnimationClock::ticks), wrapped every
// waveLoopSeconds; 0 for a value that is not finite.
[[nodiscard]] float waveSeconds(double ticks);
// `camera` modulo wavePeriod in [0, wavePeriod) per axis, computed in double; 0 for an axis that is
// not finite.
[[nodiscard]] std::array<float, 3> periodicCamera(const QcVec3 &camera);
// The colour of the water around a camera under water at `daylight` (QcSkyState::daylight, 0..1);
// a value that is not finite counts as night.
[[nodiscard]] QVector3D underwaterColour(double daylight);
// The Water block of `frame`; values that are not finite are 0 (the light's strength too, so no
// highlight comes from a broken sky).
[[nodiscard]] QcWaterUniforms uniforms(const QcWaterFrame &frame);

} // namespace QcWaterLook
