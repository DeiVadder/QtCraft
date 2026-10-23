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

    // The sun's share of the sky light, 1 at noon and 0 at night; rain and thunder each take up to
    // five sixteenths of it, as in the original.
    const double daylight = QcDayCycle::daylight(worldTime);
    double sun = (daylight - QcDayCycle::minimumDaylight) / (1.0 - QcDayCycle::minimumDaylight);
    sun *= 1.0 - std::clamp(static_cast<double>(rain), 0.0, 1.0) * 5.0 / 16.0;
    sun *= 1.0 - std::clamp(static_cast<double>(thunder), 0.0, 1.0) * 5.0 / 16.0;
    return std::clamp(static_cast<int>((1.0 - sun) * nightDarkening), 0, nightDarkening);
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
