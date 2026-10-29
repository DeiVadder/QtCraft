// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The field of view widens while the player sprints and narrows again when the sprint ends, eased
// over a fraction of a second, like the model's "FOV effects" (decision E-280). The widening is a
// factor on the field of view the player chose; the setting scales it from none to full. Flying in
// creative mode widens it a little as well (decision E-284).
class QcFovEffect
{
public:
    // Full widening: 15 % more field of view while sprinting, 10 % while flying, 25 % for both.
    static constexpr double sprintWidening = 0.15;
    static constexpr double flyWidening = 0.10;
    // Time constant of the easing: about 95 % of the way after 0.25 s, whatever the frame rate.
    static constexpr double easeSeconds = 0.08;

    struct Movement
    {
        // Sprinting and actually faster than walking (not stuck at a wall).
        bool sprinting = false;
        bool flying = false;
    };

    // `share` of the full widening, 0..1 (the "FOV effects" setting).
    void setShare(double share);
    double share() const { return m_share; }

    // Moves the factor towards the one for `movement` by `seconds` of easing and returns it.
    double advance(Movement movement, double seconds);
    // The current factor on the field of view (1 without widening).
    double factor() const { return m_factor; }
    // The factor the easing heads for.
    double target(Movement movement) const;

private:
    double m_share = 1.0;
    double m_factor = 1.0;
};
