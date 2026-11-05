// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*! \brief The clock the world's animations run on (decision E-962): campfire smoke, the runes and
    book of the enchanting tables, creatures' limbs, glowing creatures, boats and capes.

    It runs one tick length per client tick, whatever the time of day does: a world without its
    daylight cycle (the showcase, the game rule daylightCycle) keeps its animations. It stands
    while the server is paused or frozen (speed 0), runs slower at a lower tick rate and never
    faster than real time, so a sprint does not make the smoke flicker (unlike the block textures'
    QcAnimationClock). A frame shows the time between the previous and the latest tick at its
    fraction of a tick, one tick behind: the shown time never jumps, not even when the speed
    changes, and does not depend on the frame rate. */
class QcEffectClock
{
public:
    /*! One client tick at the server's \a speed (QcMsg::WorldTime::speed: 1 at its normal rate,
        0 paused or frozen). */
    void tick(double speed);
    /*! Returns the animation seconds at \a alpha (0–1) of the way from the previous tick's time
        to the latest tick's. */
    [[nodiscard]] double seconds(double alpha) const;
    /*! Returns the animation seconds at the latest tick. */
    [[nodiscard]] double time() const { return m_time; }

private:
    // The seconds at the previous and at the latest tick.
    double m_previous = 0.0;
    double m_time = 0.0;
};
