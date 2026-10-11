#pragma once

// Progress of the own player's hand animations in one frame (QcClientSession::handTimers): switching
// the held item, landing and using; 0..1 while running, negative otherwise.
struct QcHandTimers
{
    double switchProgress = -1.0;
    double landing = -1.0;
    double use = -1.0;
};
