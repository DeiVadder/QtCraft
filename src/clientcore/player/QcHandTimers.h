#pragma once

// Progress of the own player's hand animations in one frame (QcClientSession::handTimers): switching
// the held item, landing and using; 0..1 while running, negative otherwise.
struct QcHandTimers
{
    // Lengths of the animations in ticks.
    static constexpr int useTicks = 5;
    static constexpr int switchTicks = 6;
    static constexpr int landingTicks = 6;

    double switchProgress = -1.0;
    double landing = -1.0;
    double use = -1.0;
};
