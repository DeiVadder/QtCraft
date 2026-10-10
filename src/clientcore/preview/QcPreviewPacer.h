#pragma once

#include <QtGlobal>

#include <optional>

// Frame pacing of the character preview: it draws only while it is visible and at most at its
// frame rate, so an open screen with a preview adds a small, bounded load and a hidden one none.
// The clock is passed in (milliseconds of a monotonic clock), so the rules are testable.
class QcPreviewPacer
{
public:
    static constexpr int defaultFramesPerSecond = 30;
    static constexpr int maxFramesPerSecond = 60;

    int framesPerSecond() const { return m_framesPerSecond; }
    // Clamped to 1..maxFramesPerSecond.
    void setFramesPerSecond(int framesPerSecond);
    // Shortest time between two frames.
    int intervalMs() const { return 1000 / m_framesPerSecond; }

    // Whether to draw a frame now; when it says yes, the frame counts as drawn. Never while hidden;
    // the first frame after becoming visible at once, later ones an interval apart (a timer a
    // millisecond early still counts).
    bool frameDue(bool visible, qint64 nowMs);
    // Hidden: the next visible frame is drawn at once.
    void hide() { m_nextFrameMs.reset(); }

private:
    int m_framesPerSecond = defaultFramesPerSecond;
    std::optional<qint64> m_nextFrameMs;
};
