// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/figures/QcSwimmerPose.h"

#include "clientcore/session/QcClientEntity.h"
#include "common/content/mobs/QcMobDefinition.h"
#include "common/physics/QcPhysics.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace QcSwimmerPose {

namespace {

// Share of the opening a resting swimmer sways by, and the sway's speed in radians per second.
constexpr double swayShare = 0.15;
constexpr double swaySpeed = 2.4;

} // namespace

std::optional<double> forcedOpening(const QString &forcedPose)
{
    if (!forcedPose.startsWith(u"pulse:"_s)) {
        return std::nullopt;
    }
    bool ok = false;
    const double share = forcedPose.section(u':', 1).toDouble(&ok);
    return ok ? std::optional(std::clamp(share, 0.0, 1.0)) : std::nullopt;
}

double opening(double speed, double pushSpeed, double seconds, double phase)
{
    const double open = std::clamp(1.0 - speed / std::max(pushSpeed, 0.1), 0.0, 1.0);
    const double sway = 1.0 - swayShare * 0.5 * (1.0 + std::sin(seconds * swaySpeed + phase));
    return open * sway;
}

Look of(const QcMobDefinition &definition, const QcClientEntity &entity, double alpha, double seconds, std::optional<double> forced)
{
    Look look;
    look.height = definition.height * 0.5;
    look.tilt = entity.previousPitch + (entity.pitch - entity.previousPitch) * alpha;
    if (!definition.pulse) {
        return look;
    }

    // The last tick's move gives the speed; between ticks the creature glides on.
    const double speed = (entity.position - entity.previousPosition).length() * QcPhysics::tickRate;
    const double phase = static_cast<double>(entity.id % 89);
    const double open = forced.value_or(opening(speed, definition.pulse->speed, seconds, phase));
    look.spread = definition.pulse->spread * open;
    return look;
}

} // namespace QcSwimmerPose
