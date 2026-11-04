// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*! \brief The field of view widens while the player sprints and narrows again when the sprint ends,
    eased over a fraction of a second, like the model's "FOV effects" (decision E-280).

    The widening is a factor on the field of view the player chose; the setting scales it from
    none to full. Flying in creative mode widens it a little as well (decision E-284); drawing a
    bow narrows it by up to drawNarrowing with the square of the draw's share of its time
    (decision E-984). */
class QcFovEffect
{
public:
    /*! Full widening: 15 % more field of view while sprinting, 10 % while flying, 25 % for both.
        \qcshared */
    static constexpr double sprintWidening = 0.15;
    static constexpr double flyWidening = 0.10;
    /*! Narrowing of the field of view at a full draw: 15 % less. */
    static constexpr double drawNarrowing = 0.15;
    /*! Time constant of the easing: about 95 % of the way after 0.25 s, whatever the frame rate. */
    static constexpr double easeSeconds = 0.08;

    /*! \brief How the player moves this frame. */
    struct Movement
    {
        /*! Sprinting and actually faster than walking (not stuck at a wall). */
        bool sprinting = false;
        /*! Flying (creative mode). */
        bool flying = false;
        /*! The share of a launcher's draw time drawn so far (0..1), 0 when nothing is drawn
            (QcLauncherDefinition::shareOf). */
        double draw = 0.0;
    };

    /*! Returns Movement::draw for a launcher drawn with the power \a drawProgress (negative when
        nothing is drawn: QcEntityPose::drawProgress). */
    static double drawShare(double drawProgress);

    /*! \a share of the full widening, 0..1 (the "FOV effects" setting). */
    void setShare(double share);
    /*! Returns the share of the full widening (setShare()). */
    double share() const { return m_share; }

    /*! Moves the factor towards the one for \a movement by \a seconds of easing and returns it. */
    double advance(Movement movement, double seconds);
    /*! Returns the current factor on the field of view (1 without widening). */
    double factor() const { return m_factor; }
    /*! Returns the factor the easing heads for during \a movement. */
    double target(Movement movement) const;

private:
    double m_share = 1.0;
    double m_factor = 1.0;
};
