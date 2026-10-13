#pragma once

#include <QString>

// What a block does in Signal Dust circuits (decision E-333). The kind selects the behaviour the
// server implements; the numbers tune it per block, so packs and mods add their own sources (levers,
// buttons, plates), conductors and consumers ("slots": lamps, openers, pistons) in data.
enum class QcSignalKind : quint8 {
    None,
    // The wire: carries strength 15 minus one per block, connects to neighbouring dust and
    // components, climbs and descends blocks.
    Dust,
    // Sources a player switches: a lever stays, a button springs back after `pulseTicks`.
    Lever,
    Button,
    // Pressed while something stands on it (`detectsItems`: any entity, else players and creatures).
    Plate,
    // An inverter on a block: lit unless its block is powered; burns out on too fast clocks.
    Torch,
    // A diode with 1 to 4 steps of delay from back to front; side inputs of powered repeaters lock it.
    Repeater,
    // Slots: a lamp lights while powered; an opener (door, trapdoor, gate) opens on rising and closes
    // on falling power; a piston pushes up to `pushLimit` blocks and pulls one back when sticky.
    Lamp,
    Opener,
    Piston,
    // The head of an extended piston; it belongs to the piston behind it.
    PistonHead,
};

inline constexpr int qcSignalKindCount = int(QcSignalKind::PistonHead) + 1;

// How a block reacts to being pushed by a piston.
enum class QcPistonMobility : quint8 {
    // Derived from the block: unbreakable blocks stay, blocks that need support, washable ones,
    // liquids and replaceable ones break, the rest moves.
    Default,
    Push,
    // Bedrock, obsidian, an extended piston: the piston cannot extend.
    Immovable,
    // Breaks with its drops instead of moving.
    Breaks,
};

struct QcSignalDefinition
{
    static constexpr int maxPower = 15;
    static constexpr int maxPulseTicks = 1200;
    static constexpr int maxPushLimit = 64;

    QcSignalKind kind = QcSignalKind::None;
    // Strength a source emits (levers, buttons, plates, torches, repeaters).
    int power = maxPower;
    // Buttons: ticks until it springs back.
    int pulseTicks = 20;
    // Lamps: ticks until it goes dark after losing power (it lights at once).
    int offDelayTicks = 4;
    // Plates: any entity (items, armour stands) presses it, not only players and creatures.
    bool detectsItems = true;
    // Pistons.
    bool sticky = false;
    int pushLimit = 12;
    // Pistons: also powered by what would power the block above them, but only noticed with a block
    // update next to the piston (quasi-connectivity, "BUD"); the world's game rule can switch it off.
    bool quasiConnectivity = true;
    QcPistonMobility mobility = QcPistonMobility::Default;
    // Whether a full solid block passes power through (strongly and weakly powered blocks). Lamps and
    // stone conduct; pistons do not.
    bool conducts = true;
    // Torches and lamps come as two blocks, because light is a property of the block: `active` tells
    // the lit one, `counterpart` names the other (same properties). Server side only.
    bool active = true;
    QString counterpart;
    // Pistons: the block of the extended head (`facing` like the piston, `sticky` from the piston).
    QString head;

    bool isComponent() const { return kind != QcSignalKind::None; }
    // A block that exists for circuits (all but openers, which are doors and gates first).
    bool isCircuitPart() const { return isComponent() && kind != QcSignalKind::Opener; }
    // Using the block does something (switching, setting the delay) instead of placing against it.
    bool isUsable() const { return kind == QcSignalKind::Lever || kind == QcSignalKind::Button || kind == QcSignalKind::Repeater; }
};
