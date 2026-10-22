#pragma once

#include <QtGlobal>

#include <optional>

// Frame pacing of the game window (decision E-835): with a limit, frames start at most that many
// times a second, on a grid so that early or late frames do not change the rate; without one
// (0), every frame is due at once. The clock is passed in (nanoseconds of a monotonic clock), so
// the rules are testable; QcFrameLimiter asks it before every frame and times the next request.
class QcFramePacer
{
public:
    // Frames per second; 0 is no limit. Negative values count as 0.
    void setLimit(int framesPerSecond);
    int limit() const { return m_limit; }
    // Shortest time between two frames; 0 without a limit.
    qint64 intervalNs() const;

    // Whether a frame may start now; when it says yes, the frame counts as started. A frame up to a
    // millisecond early still counts (timers fire early).
    bool frameDue(qint64 nowNs);
    // Time until the next frame is due; 0 when it is due now (or without a limit).
    qint64 delayNs(qint64 nowNs) const;
    // The next frame is due at once (after a pause or a change of the limit).
    void reset() { m_nextFrameNs.reset(); }

private:
    int m_limit = 0;
    std::optional<qint64> m_nextFrameNs;
};
