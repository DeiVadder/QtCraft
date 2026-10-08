#include "QcAmbience.h"

#include "QcContentRegistry.h"

#include <algorithm>
#include <cmath>

QcAmbience::Levels QcAmbience::targets(const QcBlockAccess &world, const QcVec3 &eye)
{
    const QcContentRegistry &content = world.content();
    const auto definitionAt = [&](QcBlockPos pos) -> const QcBlockDefinition * {
        const std::optional<QcBlockId> block = world.blockAt(pos);
        return block && content.isValidBlock(*block) ? &content.block(*block) : nullptr;
    };
    const QcBlockPos head{int(std::floor(eye.x)), int(std::floor(eye.y)), int(std::floor(eye.z))};
    int ceiling = 0;
    for (int dy = 1; dy <= ceilingScan; ++dy) {
        const QcBlockDefinition *above = definitionAt(head.offset(0, dy, 0));
        if (above && above->opaque) {
            ++ceiling;
        }
    }
    Levels levels;
    const double height = std::clamp((eye.y - calmHeight) / (stormHeight - calmHeight), 0.0, 1.0);
    const double openWind = 0.3 + 0.7 * height;
    if (ceiling == 0) {
        levels.wind = float(openWind);
    } else if (ceiling < caveCeiling) {
        // Under a roof or a tree: the wind is still heard, muffled.
        levels.wind = float(openWind * 0.3);
    } else {
        levels.cave = float(0.5 + 0.5 * std::clamp(double(ceiling - caveCeiling) / 8.0, 0.0, 1.0));
    }
    const QcBlockDefinition *atHead = definitionAt(head);
    if (atHead && atHead->liquid) {
        levels.water = 1.0f;
        return levels;
    }
    int water = 0;
    for (int dx = -waterRadius; dx <= waterRadius; dx += 2) {
        for (int dy = -4; dy <= 4; dy += 2) {
            for (int dz = -waterRadius; dz <= waterRadius; dz += 2) {
                const QcBlockDefinition *block = definitionAt(head.offset(dx, dy, dz));
                water += block && block->liquid ? 1 : 0;
            }
        }
    }
    levels.water = float(std::min(1.0, double(water) / fullWaterBlocks));
    return levels;
}

QcAmbience::Levels QcAmbience::approach(Levels current, Levels target, double seconds)
{
    const auto step = float(std::max(0.0, seconds) * fadePerSecond);
    const auto move = [step](float from, float to) { return from < to ? std::min(to, from + step) : std::max(to, from - step); };
    return {move(current.wind, target.wind), move(current.cave, target.cave), move(current.water, target.water)};
}
