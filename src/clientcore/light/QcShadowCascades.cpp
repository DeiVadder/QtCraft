#include "clientcore/light/QcShadowCascades.h"

#include "clientcore/light/QcSky.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {

float smoothStep(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool isFinite(QVector3D v)
{
    return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
}

QVector3D mixed(QVector3D from, QVector3D to, float share)
{
    return from + (to - from) * share;
}

} // namespace

namespace QcShadowCascades {

Radii radii(int count, float distance)
{
    Radii result{};
    if (count < 1 || count > QcRenderOptions::maxShadowCascades || !std::isfinite(distance) || distance <= 0.0f) {
        return result;
    }
    float radius = distance;
    for (int i = count - 1; i >= 0; --i) {
        result.at(static_cast<std::size_t>(i)) = radius;
        radius *= splitRatio;
    }
    return result;
}

std::optional<Basis> basis(QVector3D toLight)
{
    if (!isFinite(toLight) || toLight.lengthSquared() < 1e-12f) {
        return std::nullopt;
    }
    const QVector3D light = toLight.normalized();
    // The light never stands on the z axis (the sun's path is tilted only a little towards it).
    const QVector3D side = QVector3D::crossProduct(QVector3D(0.0f, 0.0f, 1.0f), light);
    if (side.lengthSquared() < 1e-6f) {
        return std::nullopt;
    }
    const QVector3D right = side.normalized();
    // lookAt() with this up and the view towards -light has `right` as its x axis.
    return Basis{right, QVector3D::crossProduct(light, right), light};
}

double snapped(double value, double step)
{
    return std::floor(value / step + 0.5) * step;
}

QcShadowCascade fit(const Basis &axes, const QcVec3 &camera, QVector3D forward, float radius, int resolution)
{
    const float texel = 2.0f * radius / static_cast<float>(std::max(resolution, 1));
    const QVector3D ahead = isFinite(forward) && forward.lengthSquared() > 1e-12f ? forward.normalized() : QVector3D();
    const QVector3D middle = ahead * (forwardShift * radius);

    // The middle's coordinates along the map's axes in the world, onto the texel grid: a point of the
    // world keeps its place on the grid however the camera moves.
    const QcVec3 world = camera + QcVec3{middle.x(), middle.y(), middle.z()};
    const QcVec3 right{axes.right.x(), axes.right.y(), axes.right.z()};
    const QcVec3 up{axes.up.x(), axes.up.y(), axes.up.z()};
    const double x = world.dot(right);
    const double y = world.dot(up);
    const auto dx = static_cast<float>(snapped(x, texel) - x);
    const auto dy = static_cast<float>(snapped(y, texel) - y);
    const QVector3D centre = middle + axes.right * dx + axes.up * dy;

    QMatrix4x4 view;
    view.lookAt(centre + axes.toLight * (radius + casterReach), centre, axes.up);
    QMatrix4x4 projection;
    projection.ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + casterReach);
    return {projection * view, radius, texel};
}

QMatrix4x4 textureMatrix(bool mirrorY, bool depthZeroToOne)
{
    const float ySign = mirrorY ? -0.5f : 0.5f;
    const float zScale = depthZeroToOne ? 1.0f : 0.5f;
    const float zOffset = depthZeroToOne ? 0.0f : 0.5f;
    return QMatrix4x4(0.5f, 0.0f, 0.0f, 0.5f, //
                      0.0f, ySign, 0.0f, 0.5f, //
                      0.0f, 0.0f, zScale, zOffset, //
                      0.0f, 0.0f, 0.0f, 1.0f);
}

QcShadowLight light(const QcSkyState &sky)
{
    QcShadowLight result;
    const bool day = sky.sunDirection.y() > 0.0f;
    const QVector3D direction = day ? sky.sunDirection : sky.moonDirection;
    if (!isFinite(direction) || direction.lengthSquared() < 1e-12f) {
        return result;
    }

    result.toLight = direction.normalized();
    const float height = result.toLight.y();
    const double overcast = std::isfinite(sky.overcast) ? std::clamp(sky.overcast, 0.0, 1.0) : 1.0;
    const float clear = 1.0f - static_cast<float>(overcast);
    result.strength = smoothStep(lowestHeight, fullHeight, height) * clear * (day ? 1.0f : moonShare);
    result.litTint = day ? mixed(sunsetTint, QVector3D(1.0f, 1.0f, 1.0f), smoothStep(0.05f, 0.45f, height)) : moonTint;
    result.shadowTint = skyFillTint;
    return result;
}

} // namespace QcShadowCascades
