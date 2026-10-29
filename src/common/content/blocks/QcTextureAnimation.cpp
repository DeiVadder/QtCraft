// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/blocks/QcTextureAnimation.h"

#include <QJsonValue>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// A whole number from `low` to `high`, or nullopt.
[[nodiscard]] std::optional<int> wholeNumber(const QJsonValue &value, int low, int high)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (number != std::floor(number) || number < low || number > high) {
        return std::nullopt;
    }
    return static_cast<int>(number);
}

} // namespace

QcTextureAnimation::Frame QcTextureAnimation::frameAt(double ticks) const
{
    if (!animated()) {
        return {};
    }

    const double cycle = cycleTicks();
    double time = std::fmod(ticks, cycle);
    if (time < 0.0) {
        time += cycle;
    }
    const double position = time / frameTicks;
    const double whole = std::floor(position);
    const int index = std::clamp(static_cast<int>(whole), 0, frames - 1);
    return {index, (index + 1) % frames, interpolate ? position - whole : 0.0};
}

bool QcTextureAnimation::patternMoves(const QString &pattern)
{
    return pattern == u"fire"_s || pattern == u"lava"_s || pattern == u"water"_s;
}

QcResult<QcTextureAnimation> QcTextureAnimation::read(const QJsonObject &definition)
{
    QcTextureAnimation animation;
    if (!definition.contains(u"frames"_s)) {
        if (definition.contains(u"frameTime"_s) || definition.contains(u"interpolate"_s)) {
            return qcError(u"'frameTime' and 'interpolate' need 'frames'"_s);
        }
        return animation;
    }

    const std::optional<int> frames = wholeNumber(definition.value(u"frames"_s), 1, maxFrames);
    if (!frames) {
        return qcError(u"'frames' must be a whole number from 1 to %1"_s.arg(maxFrames));
    }
    animation.frames = *frames;

    if (definition.contains(u"frameTime"_s)) {
        const std::optional<int> ticks = wholeNumber(definition.value(u"frameTime"_s), 1, maxFrameTicks);
        if (!ticks) {
            return qcError(u"'frameTime' must be a whole number of ticks from 1 to %1"_s.arg(maxFrameTicks));
        }
        animation.frameTicks = *ticks;
    }

    const QJsonValue interpolate = definition.value(u"interpolate"_s);
    if (!interpolate.isUndefined() && !interpolate.isBool()) {
        return qcError(u"'interpolate' must be true or false"_s);
    }
    animation.interpolate = interpolate.toBool(false);
    return animation;
}
