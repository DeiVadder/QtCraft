#include "clientcore/sound/QcAmbience.h"

#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>
#include <cmath>

namespace {

// The nearest glowing liquid among the cells considered so far.
struct NearestGlowingLiquid
{
    const QcContentRegistry &content;
    std::optional<QcBlockPos> pos;
    int distance = 0;

    void consider(QcBlockPos candidate, std::optional<QcBlockId> block, int candidateDistance)
    {
        const bool glowingLiquid =
            block && content.isValidBlock(*block) && content.block(*block).liquid && content.block(*block).lightLevel > 0;
        if (glowingLiquid && (!pos || candidateDistance < distance)) {
            pos = candidate;
            distance = candidateDistance;
        }
    }
};

QcBlockPos blockOf(const QcVec3 &eye)
{
    return {static_cast<int>(std::floor(eye.x)), static_cast<int>(std::floor(eye.y)), static_cast<int>(std::floor(eye.z))};
}

// The even coordinate at or below `value`: the sampled cells lie on a grid fixed in the world, so
// that walking one block does not swap the whole sample.
int evenFloor(int value)
{
    return value - (value & 1);
}

const QcBlockDefinition *definitionAt(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    return block && world.content().isValidBlock(*block) ? &world.content().block(*block) : nullptr;
}

// Opaque blocks above `head` within QcAmbience::ceilingScan.
int ceilingAbove(const QcBlockAccess &world, QcBlockPos head)
{
    int ceiling = 0;
    for (int dy = 1; dy <= QcAmbience::ceilingScan; ++dy) {
        const QcBlockDefinition *above = definitionAt(world, head.offset(0, dy, 0));
        ceiling += above && above->opaque ? 1 : 0;
    }
    return ceiling;
}

// Wind, cave and rain of one column with `ceiling` opaque blocks above the head.
QcAmbience::Levels columnLevels(int ceiling, double openWind, double rain)
{
    QcAmbience::Levels levels;
    if (ceiling == 0) {
        levels.wind = static_cast<float>(openWind);
        levels.rain = static_cast<float>(rain);
    } else if (ceiling < QcAmbience::caveCeiling) {
        // Under a roof or a tree: the wind and the rain are still heard, muffled.
        levels.wind = static_cast<float>(openWind * QcAmbience::windUnderRoof);
        levels.rain = static_cast<float>(rain * QcAmbience::rainUnderRoof);
    } else {
        const double depth = std::clamp(static_cast<double>(ceiling - QcAmbience::caveCeiling) / 8.0, 0.0, 1.0);
        levels.cave = static_cast<float>(0.5 + 0.5 * depth);
        levels.rain = static_cast<float>(rain * QcAmbience::rainInCave);
    }
    return levels;
}

// How much the cell at `pos` adds to the water heard at `eye`: 0 without water, more when nearer.
double waterWeight(const QcBlockAccess &world, QcBlockPos pos, const QcVec3 &eye)
{
    const QcBlockDefinition *block = definitionAt(world, pos);
    if (!block || !block->liquid || block->lightLevel != 0) {
        return 0.0;
    }
    const double distance = (QcVec3{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5} - eye).length();
    return std::clamp(1.0 - distance / QcAmbience::waterFadeDistance, 0.0, 1.0);
}

// Water heard around `head`: the sampled water cells, nearer ones counting more, up to 1.
float waterLevel(const QcBlockAccess &world, const QcVec3 &eye)
{
    const QcBlockPos head = blockOf(eye);
    const QcBlockPos corner{evenFloor(head.x - QcAmbience::waterRadius), evenFloor(head.y - 4), evenFloor(head.z - QcAmbience::waterRadius)};
    double water = 0.0;
    for (int x = corner.x; x <= head.x + QcAmbience::waterRadius; x += 2) {
        for (int y = corner.y; y <= head.y + 4; y += 2) {
            for (int z = corner.z; z <= head.z + QcAmbience::waterRadius; z += 2) {
                water += waterWeight(world, {x, y, z}, eye);
            }
        }
    }
    return static_cast<float>(std::min(1.0, water / QcAmbience::fullWaterBlocks));
}

} // namespace

QcAmbience::Levels QcAmbience::targets(const QcBlockAccess &world, const QcVec3 &eye, double rain)
{
    const QcBlockPos head = blockOf(eye);
    const double height = std::clamp((eye.y - calmHeight) / (stormHeight - calmHeight), 0.0, 1.0);
    const double openWind = std::min(1.0, calmWind + (1.0 - calmWind) * height + rainWind * std::clamp(rain, 0.0, 1.0));

    // Wind, cave and rain from the columns around the head, so that a single tree or gap passing
    // by moves the levels a little instead of switching them.
    Levels levels;
    // Columns every second block, shelterRadius + 1 on a side.
    constexpr auto columns = static_cast<float>((shelterRadius + 1) * (shelterRadius + 1));
    for (int dx = -shelterRadius; dx <= shelterRadius; dx += 2) {
        for (int dz = -shelterRadius; dz <= shelterRadius; dz += 2) {
            const Levels column = columnLevels(ceilingAbove(world, head.offset(dx, 0, dz)), openWind, rain);
            levels.wind += column.wind / columns;
            levels.cave += column.cave / columns;
            levels.rain += column.rain / columns;
        }
    }

    const QcBlockDefinition *atHead = definitionAt(world, head);
    levels.water = atHead && atHead->liquid && atHead->lightLevel == 0 ? 1.0f : waterLevel(world, eye);
    return levels;
}

std::optional<QcBlockPos> QcAmbience::nearestGlowingLiquid(const QcBlockAccess &world, const QcVec3 &eye)
{
    const QcBlockPos head = blockOf(eye);
    NearestGlowingLiquid nearest{world.content(), std::nullopt, 0};
    for (int dx = -waterRadius; dx <= waterRadius; dx += 2) {
        for (int dy = -4; dy <= 4; dy += 2) {
            for (int dz = -waterRadius; dz <= waterRadius; dz += 2) {
                const QcBlockPos pos = head.offset(dx, dy, dz);
                nearest.consider(pos, world.blockAt(pos), dx * dx + dy * dy + dz * dz);
            }
        }
    }
    return nearest.pos;
}

QcAmbience::Levels QcAmbience::approach(Levels current, Levels target, double seconds)
{
    const double time = std::max(0.0, seconds);
    const auto cap = static_cast<float>(time * fadePerSecond);
    const auto share = static_cast<float>(1.0 - std::exp(-time / easeSeconds));
    // Eases in and out of each change (no corners a fast change of surroundings would make audible
    // as pulses), never faster than fadePerSecond; snaps the last bit so the levels arrive.
    const auto move = [cap, share](float from, float to) {
        const float difference = to - from;
        const float step = std::min(cap, std::abs(difference) * share);
        return std::abs(difference) <= arriveLevel ? to : from + std::copysign(step, difference);
    };
    return {move(current.wind, target.wind), move(current.cave, target.cave), move(current.water, target.water), move(current.rain, target.rain)};
}
