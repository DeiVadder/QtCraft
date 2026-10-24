#pragma once

// The time of the animated block textures in game ticks (decision E-878), advanced every frame by
// the frame's seconds at the server's speed (QcMsg::WorldTime::speed): smooth between ticks,
// faster in a /tick sprint, and standing while the server stands (pause menu, an empty server,
// /tick freeze; speed 0). It stands where it was and goes on from there, so resuming does not jump.
class QcAnimationClock
{
public:
    // One frame of `seconds` (negative: none) at `speed` server ticks per normal tick (negative: 0).
    void advance(double seconds, double speed);
    double ticks() const { return m_ticks; }

private:
    double m_ticks = 0.0;
};
