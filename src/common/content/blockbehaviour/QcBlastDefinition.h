// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*! \brief How a block takes part in explosions (decision E-600).

    Server side only. */
struct QcBlastDefinition
{
    /*! The strongest explosion a block may set off; the radius of its blast is about 1.3 times
        that. */
    static constexpr double maxPower = 8.0;
    /*! Longest fuse in ticks (\c {fuseTicks}, \c {chainFuseMin}, \c {chainFuseMax}). */
    static constexpr int maxFuseTicks = 600;
    /*! Resistance of blocks that never break in an explosion (unbreakable ones, decision E-600). */
    static constexpr double unbreakable = 3.6e6;

    /*! "blastResistance": how much of an explosion's force the block soaks up per step of a ray
        before it breaks, in the units of the reference game (stone 6, dirt 0.5, obsidian 1200).
        Negative: derived from the block (effectiveResistance). */
    double resistance = -1.0;
    /*! "explosive": {"power", "fuse", "chainFuseMin", "chainFuseMax", "fire"}: the block primes
        when lit (flint and steel, fire, power from circuits, another explosion), counts
        \c {fuse} ticks down (a random time between chainFuseMin and chainFuseMax when another
        explosion lit it) and explodes with \c {power}; with \c {fire} the blast lights fires
        around it. \c {power} defaults to 4 in an \c {"explosive"} object; 0: not explosive.
        \qcshared */
    double power = 0.0;
    int fuseTicks = 80;
    int chainFuseMin = 10;
    int chainFuseMax = 30;
    bool fire = false;

    /*! Returns whether the block explodes (it has a \c {power}). */
    bool isExplosive() const { return power > 0.0; }
    /*! Returns the resistance that counts for a block of \a hardness (seconds by hand, negative:
        unbreakable) that is a liquid if \a liquid is true: the explicit one, else unbreakable
        blocks never break (a liquid soaks up as much as the reference game's water, 100) and the
        others resist as much as their hardness. */
    double effectiveResistance(double hardness, bool liquid) const
    {
        if (resistance >= 0.0) {
            return resistance;
        }
        if (hardness < 0.0) {
            return liquid ? liquidResistance : unbreakable;
        }
        return hardness;
    }

    /*! Resistance of an unbreakable liquid without an explicit one (the reference game's water). */
    static constexpr double liquidResistance = 100.0;
};
