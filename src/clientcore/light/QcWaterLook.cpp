// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/light/QcWaterLook.h"

#include "clientcore/meshing/QcBlockRenderTable.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcBlockAccess.h"

#include <algorithm>
#include <cmath>

namespace {

// A float of `value`, 0 when it is not finite.
float finiteOrZero(double value)
{
    return std::isfinite(value) ? static_cast<float>(value) : 0.0f;
}

std::array<float, 4> vec4(QVector3D v, double w)
{
    return {finiteOrZero(v.x()), finiteOrZero(v.y()), finiteOrZero(v.z()), finiteOrZero(w)};
}

} // namespace

bool QcWaterLook::eyeUnderWater(const QcBlockAccess &world, const QcVec3 &eye)
{
    if (!std::isfinite(eye.y) || !std::isfinite(eye.x) || !std::isfinite(eye.z)) {
        return false;
    }
    const QcBlockPos cell{static_cast<int>(std::floor(eye.x)), static_cast<int>(std::floor(eye.y)), static_cast<int>(std::floor(eye.z))};
    if (!QcPhysics::isWaterAt(world, cell)) {
        return false;
    }
    if (QcPhysics::isWaterAt(world, {cell.x, cell.y + 1, cell.z})) {
        return true;
    }
    const std::optional<QcBlockId> block = world.blockAt(cell);
    const double surface = block ? QcBlockRenderTable::liquidHeight(world.content(), *block) : 1.0;
    return eye.y - std::floor(eye.y) < surface;
}

float QcWaterLook::waveSeconds(double ticks)
{
    if (!std::isfinite(ticks)) {
        return 0.0f;
    }
    const double seconds = ticks / 20.0;
    const double wrapped = seconds - waveLoopSeconds * std::floor(seconds / waveLoopSeconds);
    return static_cast<float>(std::clamp(wrapped, 0.0, waveLoopSeconds));
}

std::array<float, 3> QcWaterLook::periodicCamera(const QcVec3 &camera)
{
    const auto wrap = [](double value) {
        if (!std::isfinite(value)) {
            return 0.0f;
        }
        const double wrapped = value - wavePeriod * std::floor(value / wavePeriod);
        // A hair below a whole period rounds up to it in float; the pattern repeats there anyway.
        return static_cast<float>(std::clamp(wrapped, 0.0, wavePeriod));
    };
    return {wrap(camera.x), wrap(camera.y), wrap(camera.z)};
}

QVector3D QcWaterLook::underwaterColour(double daylight)
{
    const double light = std::isfinite(daylight) ? std::clamp(daylight, 0.0, 1.0) : 0.0;
    const auto share = static_cast<float>(nightScatter + (1.0 - nightScatter) * light);
    return QVector3D(scatter[0], scatter[1], scatter[2]) * share;
}

QcWaterUniforms QcWaterLook::uniforms(const QcWaterFrame &frame)
{
    QcWaterUniforms result;
    const QMatrix4x4 inverse = frame.viewProjection.inverted();
    for (std::size_t i = 0; i < result.inverseViewProjection.size(); ++i) {
        result.inverseViewProjection[i] = finiteOrZero(inverse.constData()[i]);
    }

    const QcSkyState &sky = frame.sky;
    result.zenith = vec4(sky.zenith, frame.seconds);
    result.horizon = vec4(sky.horizon, frame.depthZeroToOne ? 1.0 : 0.0);
    result.glow = vec4(sky.glow, frame.textureYFollowsNdc ? 1.0 : -1.0);
    const QVector3D toLight = frame.light.toLight;
    const bool lightFinite = std::isfinite(toLight.x()) && std::isfinite(toLight.y()) && std::isfinite(toLight.z())
                             && std::isfinite(frame.light.strength) && toLight.lengthSquared() > 0.0f;
    result.toLight = lightFinite ? vec4(toLight.normalized(), std::clamp(frame.light.strength, 0.0f, 1.0f))
                                 : std::array<float, 4>{0.0f, 1.0f, 0.0f, 0.0f};
    result.lightColour = vec4(frame.light.litTint, 0.0);

    const std::array<float, 3> camera = periodicCamera(frame.camera);
    result.camera = {camera[0], camera[1], camera[2], 0.0f};
    const QcRenderOptions::Water &water = frame.water;
    const float width = frame.size.width() > 0 ? 1.0f / static_cast<float>(frame.size.width()) : 0.0f;
    const float height = frame.size.height() > 0 ? 1.0f / static_cast<float>(frame.size.height()) : 0.0f;
    result.pixel = {width, height, static_cast<float>(water.waveOctaves), finiteOrZero(water.refraction)};
    result.effects = {static_cast<float>(water.reflectionSteps), finiteOrZero(water.caustics), 0.0f, 0.0f};
    result.extinction = {extinction[0], extinction[1], extinction[2], 0.0f};
    result.deep = vec4(underwaterColour(frame.sky.daylight), 0.0);
    return result;
}
