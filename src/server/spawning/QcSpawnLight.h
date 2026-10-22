#pragma once

#include <QRandomGenerator>

// The light test of monsters (decision E-836), as in the original game: a cell is dark enough when
// its sky light passes a random draw of 0..31, its block light (torches) is at most the rule's
// `maxLight` (0 for monsters) and its brightness, the larger of block light and the sky light less
// the darkening of the sky by time and weather, is at most a random threshold of 0..7. Day or night
// is no condition of its own: by day the open sky is far too bright, while caves, the shade of thick
// roofs and dense forests stay dark enough; at night the dimmed sky lets most open ground pass, the
// more often the darker it is. Spawn rules with `"darkness": false` (slime chunks) skip the test.
namespace QcSpawnLight {

// Levels the sky light loses at night (midnight in clear weather).
inline constexpr int nightDarkening = 11;
// Levels it loses during a thunderstorm, by day and by night.
inline constexpr int thunderDarkening = 10;
// A thunderstorm darkens the sky by thunderDarkening from this strength on.
inline constexpr float thunderStrength = 0.9f;
// The threshold the brightness is compared with lies in 0..maxThreshold.
inline constexpr int maxThreshold = 7;
// The sky light must not exceed a draw of 0..skyChance - 1.
inline constexpr int skyChance = 32;

// Levels the sky light loses at `worldTime` with rain and thunder of strengths 0..1: 0 at noon in
// clear weather, nightDarkening at midnight, rain dims the day a little, a storm as far as
// thunderDarkening.
int skyDarkening(double worldTime, float rain, float thunder);

// The random draws of one test: the sky's draw (0..skyChance - 1) and the threshold (0..maxThreshold).
struct Draws
{
    int sky = 0;
    int threshold = 0;
};
Draws draw(QRandomGenerator &random);

// The sky light, block light and darkening of a cell, and the brightest block light allowed.
struct Cell
{
    int skyLight = 0;
    int blockLight = 0;
    int darkening = 0;
    int maxBlockLight = 0;
};
// Whether the cell is dark enough for a monster with these draws.
bool darkEnough(Cell cell, Draws draws);

} // namespace QcSpawnLight
