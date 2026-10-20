#pragma once

#include <QHash>
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
    // Stage 29.2 (decision E-342 ff.). A diode that reads the block behind it (container fullness,
    // readable blocks) and compares or subtracts its side inputs.
    Comparator,
    // Sends a pulse out of its back when the block state in front of it changes.
    Observer,
    // Moves items between containers; any power locks it.
    Hopper,
    // On rising power ejects one item after a short delay: uses it (`usesItems`, dispenser) or
    // throws it (dropper); both put it into a container in front.
    Dispenser,
    // Stage 32.2 (decision E-384). A rail slot (powered and activator rails): powered while it gets
    // power or while a rail of the same block at most `railReach` rails along its line gets power.
    Rail,
    // Stage 51.2 (decision E-601). A slot that primes the explosive it is (QcBlastDefinition) as soon
    // as it gets power.
    Igniter,
    // A source that is always on (a block of Signal Dust, decision E-752): it gives
    // `power` to the dust and components on all six sides, but never powers or conducts through
    // the blocks next to it.
    Constant,
    // A slot that plays a note when its power rises (decision E-767); using it raises
    // the pitch by a semitone. Its instrument comes from the block below (QcNoteInstruments).
    Note,
    // A source with the strength of its state `power`, set by a projectile hitting it (the closer to
    // the centre of the face, the stronger) and released after a short time (decision E-768). Like
    // `constant` it powers dust and components on all sides, but no block.
    Target,
};

inline constexpr int qcSignalKindCount = int(QcSignalKind::Target) + 1;

// The instruments of a note block (decision E-767): the sound it plays for the block below it, by
// that block's name, else by its sound material, else `fallback`. Names and sounds are qualified.
struct QcNoteInstruments
{
    QString fallback;
    QHash<QString, QString> byBlock;
    QHash<QString, QString> bySound;

    QString of(const QString &block, const QString &soundMaterial) const
    {
        return byBlock.value(block, bySound.value(soundMaterial, fallback));
    }
};

// What a Signal Comparator reads from a block behind it (decision E-343).
enum class QcComparatorReading : quint8 {
    None,
    // Fullness of the block's container or machine slots (the reference formula).
    Contents,
    // The index of the value of a state property, scaled to 0..15.
    Property,
    // The fullness of a container minecart on the block (a detector rail, decision E-385); without
    // one the comparator takes the block's power as usual.
    Carts,
    // Twice the bites left of a block eaten bite by bite (cake: 14 whole, 2 for the last bite;
    // decision E-773).
    Bites,
};

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
    static constexpr int maxRailReach = 64;
    // Note blocks: semitones from the lowest note (F#3, 0) to the highest (F#5, 24); instruments.
    static constexpr int noteCount = 25;
    static constexpr int maxInstruments = 64;

    QcSignalKind kind = QcSignalKind::None;
    // Strength a source emits (levers, buttons, plates, torches, repeaters).
    int power = maxPower;
    // Buttons: ticks until it springs back.
    int pulseTicks = 20;
    // Lamps: ticks until it goes dark after losing power (it lights at once).
    int offDelayTicks = 4;
    // Plates: any entity (items, armour stands) presses it, not only players and creatures.
    bool detectsItems = true;
    // Plates: only minecarts press it (a detector rail, decision E-385).
    bool detectsCarts = false;
    // Rail slots: how many rails of the same block along the line pass power on.
    int railReach = 8;
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
    // Torches: strongly power a conductor above them (stone on top passes the power on); the small
    // Signal Torch does not, though it still powers a component above it directly (decision E-745).
    bool strongPowersAbove = true;
    // Pistons: the block of the extended head (`facing` like the piston, `sticky` from the piston).
    QString head;
    // Dispensers: use items with a dispense action (else every item is thrown, a dropper).
    bool usesItems = true;
    // What a comparator reads from this block (any block, also one without a kind) and the property
    // it reads for QcComparatorReading::Property.
    QcComparatorReading reading = QcComparatorReading::None;
    QString readProperty;
    // Note blocks: their instruments.
    QcNoteInstruments instruments;
    // Targets: ticks a hit keeps its strength, for projectiles that stick (arrows) and others.
    int stuckHitTicks = 20;
    int hitTicks = 8;

    bool isComponent() const { return kind != QcSignalKind::None; }
    // A block that exists for circuits (all but openers, which are doors and gates first).
    bool isCircuitPart() const { return isComponent() && kind != QcSignalKind::Opener; }
    // Using the block does something (switching, setting the delay) instead of placing against it.
    // Comparators switch between comparing and subtracting.
    bool isUsable() const
    {
        return kind == QcSignalKind::Lever || kind == QcSignalKind::Button || kind == QcSignalKind::Repeater || kind == QcSignalKind::Comparator
               || kind == QcSignalKind::Note;
    }
};
