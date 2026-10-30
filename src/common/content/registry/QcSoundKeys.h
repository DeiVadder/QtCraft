// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"

#include <QString>

/*! \brief Names of the sounds the game plays for blocks, creatures and the player.

    Blocks name a sound material; the sound of an action is
    "<namespace>:block.<material>.<action>" unless the block gives an explicit key for that
    action. */
namespace QcSoundKeys {

/*! The player taking damage. */
inline constexpr auto playerHurt = "qtcraft:player.hurt";
/*! The player dying. */
inline constexpr auto playerDeath = "qtcraft:player.death";
/*! The player eating. */
inline constexpr auto playerEat = "qtcraft:player.eat";
/*! The player drinking. */
inline constexpr auto playerDrink = "qtcraft:player.drink";
/*! An item picked up. */
inline constexpr auto itemPickup = "qtcraft:item.pickup";
/*! The player falling into water. */
inline constexpr auto waterSplash = "qtcraft:water.splash";
/*! A swimming stroke while the player moves through water. */
inline constexpr auto waterSwim = "qtcraft:water.swim";
/*! A click in the user interface. */
inline constexpr auto uiClick = "qtcraft:ui.click";
/*! The wind ambience loop. */
inline constexpr auto ambientWind = "qtcraft:ambient.wind";
/*! The cave ambience loop. */
inline constexpr auto ambientCave = "qtcraft:ambient.cave";
/*! The water ambience loop. */
inline constexpr auto ambientWater = "qtcraft:ambient.water";
/*! Rain falling around the listener and thunder after lightning (decision E-395). \qcshared */
inline constexpr auto ambientRain = "qtcraft:ambient.rain";
inline constexpr auto weatherThunder = "qtcraft:weather.thunder";
/*! A creature taking damage, for creatures that name no own sound. */
inline constexpr auto mobHurt = "qtcraft:mob.generic.hurt";
/*! A creature dying, for creatures that name no own sound. */
inline constexpr auto mobDeath = "qtcraft:mob.generic.death";
/*! A working machine, for machines that name no own sound. */
inline constexpr auto machineActive = "qtcraft:machine.furnace";
/*! Two liquids turning into stone, a bucket taking or pouring a liquid, a bottle catching a
    creature. \qcshared */
inline constexpr auto liquidHiss = "qtcraft:liquid.hiss";
inline constexpr auto bucketFill = "qtcraft:item.bucket.fill";
inline constexpr auto bucketEmpty = "qtcraft:item.bucket.empty";
inline constexpr auto bottleCatch = "qtcraft:item.bottle.catch";
/*! Armour put on or taken off, and a worn piece or tool breaking. \qcshared */
inline constexpr auto armourEquip = "qtcraft:item.armour.equip";
inline constexpr auto itemBreak = "qtcraft:item.break";
/*! Lava near the listener now and then. */
inline constexpr auto lavaPop = "qtcraft:liquid.lava.pop";
/*! Circuits (decision E-336): a lever or button switching, a piston moving out and in, a Signal
    Torch burning out. \qcshared */
inline constexpr auto signalClick = "qtcraft:block.signal.click";
inline constexpr auto pistonOut = "qtcraft:block.piston.out";
inline constexpr auto pistonIn = "qtcraft:block.piston.in";
inline constexpr auto torchFizz = "qtcraft:block.torch.fizz";
/*! A dispenser or dropper ejecting an item, and one with nothing to eject or no room (decision
    E-345). \qcshared */
inline constexpr auto dispenserDispense = "qtcraft:block.dispenser.dispense";
inline constexpr auto dispenserFail = "qtcraft:block.dispenser.fail";
/*! Flint and steel striking a fire (decision E-393). */
inline constexpr auto flintAndSteel = "qtcraft:item.flint_and_steel.use";
/*! A collected experience orb and a level gained (decision E-398). \qcshared */
inline constexpr auto experienceOrb = "qtcraft:player.experience";
inline constexpr auto levelUp = "qtcraft:player.levelup";
/*! A launcher drawn and shooting (decision E-425); projectiles name their own sounds.
    \qcshared */
inline constexpr auto bowDraw = "qtcraft:item.bow.draw";
inline constexpr auto bowShoot = "qtcraft:item.bow.shoot";
/*! An explosive lit (its fuse hisses) and an explosion (decision E-604). \qcshared */
inline constexpr auto explosivePrimed = "qtcraft:entity.explosive.primed";
inline constexpr auto explosion = "qtcraft:entity.explosive.explode";
/*! Material of blocks that do not name one and are not obviously wood, earth, glass or plants. */
inline constexpr auto defaultMaterial = "qtcraft:stone";

/*! Returns the name of \a action in sound keys: "break", "place", "step" or "hit". */
QString actionName(QcBlockSoundAction action);
/*! Returns the material \a block gets when it names no "sound": none for invisible blocks and
    liquids, otherwise derived from its tool, shape and render layer. */
QString defaultMaterialFor(const QcBlockDefinition &block);
/*! Returns the sound of \a action on \a block: its explicit key for the action, otherwise
    "<namespace>:block.<material>.<action>"; empty when the block has no material. */
QString blockSound(const QcBlockDefinition &block, QcBlockSoundAction action);

} // namespace QcSoundKeys
