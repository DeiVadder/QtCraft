// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QString>

/*! \brief What a block does in Signal Dust circuits (decision E-333).

    The kind selects the behaviour the server implements; the numbers tune it per block, so packs
    and mods add their own sources (levers, buttons, plates), conductors and consumers ("slots":
    lamps, openers, pistons) in data. */
enum class QcSignalKind : quint8 {
    None, // Not part of circuits.
    /*! The wire: carries strength 15 minus one per block, connects to neighbouring dust and
        components, climbs and descends blocks. */
    Dust,
    /*! Sources a player switches: a lever stays, a button springs back after \c {pulseTicks}. */
    Lever,
    Button, // A source that springs back after \c {pulseTicks}.
    /*! Pressed while something stands on it (\c {detectsItems}: any entity, else players and
        creatures). */
    Plate,
    /*! An inverter on a block: lit unless its block is powered; burns out on too fast clocks. */
    Torch,
    /*! A diode with 1 to 4 steps of delay from back to front; side inputs of powered repeaters
        lock it. */
    Repeater,
    /*! Slots: a lamp lights while powered; an opener (door, trapdoor, gate) opens on rising and
        closes on falling power; a piston pushes up to \c {pushLimit} blocks and pulls one back
        when sticky. */
    Lamp,
    Opener, // A door, trapdoor or gate that opens on rising and closes on falling power.
    Piston, // Pushes up to \c {pushLimit} blocks while powered; pulls one back when sticky.
    /*! The head of an extended piston; it belongs to the piston behind it. */
    PistonHead,
    /*! Stage 29.2 (decision E-342 ff.). A diode that reads the block behind it (container
        fullness, readable blocks) and compares or subtracts its side inputs. */
    Comparator,
    /*! Sends a pulse out of its back when the block state in front of it changes. */
    Observer,
    /*! Moves items between containers; any power locks it. */
    Hopper,
    /*! On rising power ejects one item after a short delay: uses it (\c {usesItems}, dispenser)
        or throws it (dropper); both put it into a container in front. */
    Dispenser,
    /*! Stage 32.2 (decision E-384). A rail slot (powered and activator rails): powered while it
        gets power or while a rail of the same block at most \c {railReach} rails along its line
        gets power. */
    Rail,
    /*! Stage 51.2 (decision E-601). A slot that primes the explosive it is (QcBlastDefinition)
        as soon as it gets power. */
    Igniter,
    /*! A source that is always on (a block of Signal Dust, decision E-752): it gives \c {power}
        to the dust and components on all six sides, but never powers or conducts through the
        blocks next to it. */
    Constant,
    /*! A slot that plays a note when its power rises (decision E-767); using it raises
        the pitch by a semitone. Its instrument comes from the block below (QcNoteInstruments). */
    Note,
    /*! A source with the strength of its state \c {power}, set by a projectile hitting it (the
        closer to the centre of the face, the stronger) and released after a short time (decision
        E-768). Like \c {constant} it powers dust and components on all sides, but no block. */
    Target,
    /*! Stage 62.3 (decision E-782). A tripwire hook on the side of a block: a source like a
        button while \c {powered} (strongly into the block it hangs on). It is \c {attached}
        while a hook facing it waits at most \c {wireReach} cells of tripwire away; then both are
        powered while something touches the wire between them. */
    Hook,
    /*! The wire between two hooks: \c {powered} while an entity touches it (released when
        nothing has for \c {pulseTicks}), \c {attached} while it belongs to a line of two hooks,
        \c {disarmed} once shears cut it (it then breaks without setting the hooks off). */
    Tripwire,
    /*! A source with the strength of its state \c {power}, the number of players looking into
        the container (the trapped chest, decision E-783): it powers dust and components on all
        sides and the block below it strongly. */
    Viewers,
};

/*! Number of values of QcSignalKind. */
inline constexpr int qcSignalKindCount = static_cast<int>(QcSignalKind::Viewers) + 1;

/*! \brief The instruments of a note block (decision E-767): the sound it plays for the block below
    it, by that block's name, else by its sound material, else \c {fallback}.

    Names and sounds are qualified. */
struct QcNoteInstruments
{
    /*! Sound for any other block below (\c {"default"}). */
    QString fallback;
    /*! Sound by name of the block below (\c {"blocks"}, at most
        QcSignalDefinition::maxInstruments). */
    QHash<QString, QString> byBlock;
    /*! Sound by sound material of the block below (\c {"sounds"}, at most
        QcSignalDefinition::maxInstruments). */
    QHash<QString, QString> bySound;

    /*! Returns the sound for the block \a block below with the sound material \a soundMaterial. */
    QString of(const QString &block, const QString &soundMaterial) const
    {
        return byBlock.value(block, bySound.value(soundMaterial, fallback));
    }
};

/*! \brief What a Signal Comparator reads from a block behind it (decision E-343). */
enum class QcComparatorReading : quint8 {
    None, // Nothing to read (no \c {"comparator"}); the comparator takes the block's power.
    /*! Fullness of the block's container or machine slots (the reference formula). */
    Contents,
    /*! The index of the value of a state property, scaled to 0..15. */
    Property,
    /*! The fullness of a container minecart on the block (a detector rail, decision E-385);
        without one the comparator takes the block's power as usual. */
    Carts,
    /*! Twice the bites left of a block eaten bite by bite (cake: 14 whole, 2 for the last bite;
        decision E-773). */
    Bites,
};

/*! \brief How a block reacts to being pushed by a piston. */
enum class QcPistonMobility : quint8 {
    /*! Derived from the block: unbreakable blocks stay, blocks that need support, washable ones,
        liquids and replaceable ones break, the rest moves. */
    Default,
    Push, // Always moves (\c {"movable": "push"}).
    /*! Bedrock, obsidian, an extended piston: the piston cannot extend. */
    Immovable,
    /*! Breaks with its drops instead of moving. */
    Breaks,
};

/*! \brief The Signal Dust part of a block (the \c {"signal"} object, decision E-333): its kind
    and the numbers that tune it.

    Only \c {kind} reaches the client; the rest is server side only. */
struct QcSignalDefinition
{
    /*! Highest signal strength. */
    static constexpr int maxPower = 15;
    /*! Longest \c {pulseTicks}, \c {offDelayTicks}, \c {stuckHitTicks} and \c {hitTicks}. */
    static constexpr int maxPulseTicks = 1200;
    /*! Largest \c {pushLimit}. */
    static constexpr int maxPushLimit = 64;
    /*! Largest \c {railReach}. */
    static constexpr int maxRailReach = 64;
    /*! Largest \c {wireReach}. */
    static constexpr int maxWireReach = 64;
    /*! Note blocks: number of notes, semitones from the lowest note (F#3, 0) to the highest
        (F#5, 24). */
    static constexpr int noteCount = 25;
    /*! Note blocks: most entries of each instrument map (QcNoteInstruments). */
    static constexpr int maxInstruments = 64;

    /*! Kind of circuit part (\c {"type"}); None for blocks outside circuits. */
    QcSignalKind kind = QcSignalKind::None;
    /*! Strength a source emits (levers, buttons, plates, torches, repeaters). */
    int power = maxPower;
    /*! Buttons: ticks until it springs back. */
    int pulseTicks = 20;
    /*! Lamps: ticks until it goes dark after losing power (it lights at once). */
    int offDelayTicks = 4;
    /*! Plates: any entity (items, armour stands) presses it, not only players and creatures. */
    bool detectsItems = true;
    /*! Plates: only minecarts press it (a detector rail, decision E-385). */
    bool detectsCarts = false;
    /*! Rail slots: how many rails of the same block along the line pass power on. */
    int railReach = 8;
    /*! Hooks: how many cells of tripwire may lie between two hooks of one line. */
    int wireReach = 39;
    /*! Pistons: whether it pulls a block back (\c {"sticky"}) and the most blocks it pushes
        (\c {"pushLimit"}). \qcshared */
    bool sticky = false;
    int pushLimit = 12;
    /*! Pistons: also powered by what would power the block above them, but only noticed with a
        block update next to the piston (quasi-connectivity, "BUD"); the world's game rule can
        switch it off. */
    bool quasiConnectivity = true;
    /*! How the block reacts to a piston pushing it (\c {"movable"}: default, push, immovable or
        breaks). */
    QcPistonMobility mobility = QcPistonMobility::Default;
    /*! Whether a full solid block passes power through (strongly and weakly powered blocks).
        Lamps and stone conduct; pistons do not. */
    bool conducts = true;
    /*! Torches and lamps come as two blocks, because light is a property of the block:
        \c {active} tells the lit one, \c {counterpart} names the other (same properties). Server
        side only. \qcshared */
    bool active = true;
    QString counterpart;
    /*! Torches: strongly power a conductor above them (stone on top passes the power on); the
        small Signal Torch does not, though it still powers a component above it directly
        (decision E-745). */
    bool strongPowersAbove = true;
    /*! Pistons: the block of the extended head (\c {facing} like the piston, \c {sticky} from
        the piston). */
    QString head;
    /*! Dispensers: use items with a dispense action (else every item is thrown, a dropper). */
    bool usesItems = true;
    /*! What a comparator reads from this block (any block, also one without a kind) and the
        property it reads for QcComparatorReading::Property (\c {"comparator"}). \qcshared */
    QcComparatorReading reading = QcComparatorReading::None;
    QString readProperty;
    /*! Note blocks: their instruments. */
    QcNoteInstruments instruments;
    /*! Targets: ticks a hit keeps its strength, for projectiles that stick (arrows) and others.
        \qcshared */
    int stuckHitTicks = 20;
    int hitTicks = 8;

    /*! Returns whether the block takes part in circuits (it has a kind). */
    bool isComponent() const { return kind != QcSignalKind::None; }
    /*! Returns whether the block exists for circuits (all components but openers, which are
        doors and gates first). */
    bool isCircuitPart() const { return isComponent() && kind != QcSignalKind::Opener; }
    /*! Returns whether using the block does something (switching, setting the delay, a note)
        instead of placing against it. Comparators switch between comparing and subtracting. */
    bool isUsable() const
    {
        return kind == QcSignalKind::Lever || kind == QcSignalKind::Button || kind == QcSignalKind::Repeater || kind == QcSignalKind::Comparator
               || kind == QcSignalKind::Note;
    }
};
