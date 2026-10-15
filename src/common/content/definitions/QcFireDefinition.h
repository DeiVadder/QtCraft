#pragma once

// How a block takes part in fire (decision E-392). Server side only.
struct QcFireDefinition
{
    // Chances are given per 100 (ignite) and per 300 (burn), like the reference game.
    static constexpr int maxChance = 100;
    static constexpr int maxAlightSeconds = 60;

    // The block is fire ("fire": true): it burns where it stands, spreads to flammable blocks near it
    // and goes out on its own (QcServerFire).
    bool isFire = false;
    // "flammable": {"ignite": 0-100, "burn": 0-100}. `ignite`: how readily fire near the block
    // catches on in the air next to it (leaves and wool high, wood low); `burn`: how readily fire
    // next to it burns the block away (it becomes fire or air).
    int ignite = 0;
    int burn = 0;
    // "eternalFire": true: fire standing on the block never ages out and never spreads (the magma
    // block as a hearth).
    bool eternal = false;
    // "setsAlight": seconds a body touching the block burns for (fire 8, lava 15; decision E-393):
    // it takes QcBodyBurning's damage every second until the time is over or water puts it out.
    int setsAlightSeconds = 0;
    // "sparks": chance in 100 that a block update of the block (lava) lights fire in an air cell
    // right next to it that touches a flammable block (QcFireIgnition, decision E-393).
    int sparks = 0;

    bool isFlammable() const { return ignite > 0 || burn > 0; }
};
