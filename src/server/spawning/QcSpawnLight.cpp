// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/spawning/QcSpawnLight.h"

#include "common/world/QcDayCycle.h"

#include <algorithm>
#include <cmath>

namespace QcSpawnLight {

int skyDarkening(double worldTime, float rain, float thunder)
{
    if (thunder >= thunderStrength) {
        return thunderDarkening;
    }

    return QcDayCycle::skyDarkening(worldTime, rain, thunder);
}

Draws draw(QRandomGenerator &random)
{
    Draws draws;
    draws.sky = random.bounded(skyChance);
    draws.threshold = random.bounded(maxThreshold + 1);
    return draws;
}

bool darkEnough(Cell cell, Draws draws)
{
    if (cell.skyLight > draws.sky || cell.blockLight > cell.maxBlockLight) {
        return false;
    }
    const int brightness = std::max(cell.skyLight - cell.darkening, cell.blockLight);
    return brightness <= draws.threshold;
}

} // namespace QcSpawnLight
