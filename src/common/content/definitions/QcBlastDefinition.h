#pragma once

// How a block takes part in explosions (decision E-600). Server side only.
struct QcBlastDefinition
{
    // The strongest explosion a block may set off; the radius of its blast is about 1.3 times that.
    static constexpr double maxPower = 8.0;
    static constexpr int maxFuseTicks = 600;
    // Resistance of blocks that never break in an explosion (unbreakable ones, decision E-600).
    static constexpr double unbreakable = 3.6e6;

    // "blastResistance": how much of an explosion's force the block soaks up per step of a ray
    // before it breaks, in the units of the reference game (stone 6, dirt 0.5, obsidian 1200).
    // Negative: derived from the block (effectiveResistance).
    double resistance = -1.0;
    // "explosive": {"power", "fuse", "chainFuseMin", "chainFuseMax", "fire"}: the block primes when
    // lit (flint and steel, fire, power from circuits, another explosion), counts `fuse` ticks down
    // (a random time between chainFuseMin and chainFuseMax when another explosion lit it) and
    // explodes with `power`; with `fire` the blast lights fires around it.
    double power = 0.0;
    int fuseTicks = 80;
    int chainFuseMin = 10;
    int chainFuseMax = 30;
    bool fire = false;

    bool isExplosive() const { return power > 0.0; }
    // The resistance that counts for a block of `hardness` (seconds by hand, negative: unbreakable):
    // the explicit one, else unbreakable blocks never break (a liquid soaks up as much as the
    // reference game's water, 100) and the others resist as much as their hardness.
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

    static constexpr double liquidResistance = 100.0;
};
