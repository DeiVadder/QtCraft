#pragma once

#include "QcCapeSwing.h"

#include <QHash>

#include <limits>

// The cape swings of the figures in the world (decision E-240), one per entity, advanced once per
// frame. A figure seen for the first time starts at rest on its target; one that was not drawn in
// a frame loses its swing.
class QcCapeTracker
{
public:
    // Key of the local player, who is no entity of the session.
    static constexpr quint32 localPlayer = std::numeric_limits<quint32>::max();

    // A new frame `seconds` after the last one; `time` is the clock driving idle sway and flutter.
    void beginFrame(double seconds, double time);
    // The cape of figure `id` moved on by this frame's time.
    QcCapeSwing::Angles advance(quint32 id, const QcCapeDrive &drive);
    // Drops the swings of figures not advanced since beginFrame().
    void endFrame();
    qsizetype size() const { return m_swings.size(); }

private:
    struct Tracked
    {
        QcCapeSwing swing;
        quint64 frame = 0;
    };

    QHash<quint32, Tracked> m_swings;
    quint64 m_frame = 0;
    double m_seconds = 0.0;
    double m_time = 0.0;
};
