// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcAirSupply.h"

#include "common/physics/QcBoatMotion.h"
#include "common/physics/QcPhysics.h"

#include <algorithm>
#include <cmath>

void QcAirSupply::restore(int air)
{
    setAir(std::clamp(air, 0, maxAir));
    setDrowningTicks(0);
}

void QcAirSupply::reset()
{
    restore(maxAir);
}

bool QcAirSupply::tick(bool submerged)
{
    if (!submerged) {
        setAir(std::min(maxAir, air() + refillPerTick));
        setDrowningTicks(0);
        return false;
    }
    if (air() > 0) {
        setAir(air() - 1);
        return false;
    }

    // The first hit a second after the last breath, then one every second, as in the reference.
    setDrowningTicks(drowningTicks() + 1);
    if (drowningTicks() < damageIntervalTicks) {
        return false;
    }
    setDrowningTicks(0);
    return true;
}

int QcAirSupply::bubblesOf(int air, int full)
{
    if (full <= 0) {
        return 0;
    }
    const int clamped = std::clamp(air, 0, full);
    return (clamped * bubbles + full - 1) / full;
}

bool QcAirSupply::submergedAt(const QcBlockAccess &world, const QcVec3 &eyes)
{
    if (!std::isfinite(eyes.x) || !std::isfinite(eyes.y) || !std::isfinite(eyes.z)) {
        return false;
    }
    const QcBlockPos cell = QcPhysics::blockContaining(eyes);
    if (!QcPhysics::isWaterAt(world, cell)) {
        return false;
    }
    // No surface: falling water, which fills its cell.
    const std::optional<double> surface = QcBoatMotion::waterSurfaceAt(world, cell);
    return !surface || eyes.y < *surface;
}
