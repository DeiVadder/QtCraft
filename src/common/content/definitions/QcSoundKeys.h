#pragma once

#include "common/content/definitions/QcBlockDefinition.h"

#include <QString>

// Names of the sounds the game plays for blocks, creatures and the player. Blocks name a sound
// material; the sound of an action is "<namespace>:block.<material>.<action>" unless the block
// gives an explicit key for that action.
namespace QcSoundKeys {

inline constexpr auto playerHurt = "qtcraft:player.hurt";
inline constexpr auto playerDeath = "qtcraft:player.death";
inline constexpr auto playerEat = "qtcraft:player.eat";
inline constexpr auto playerDrink = "qtcraft:player.drink";
inline constexpr auto itemPickup = "qtcraft:item.pickup";
inline constexpr auto waterSplash = "qtcraft:water.splash";
inline constexpr auto waterSwim = "qtcraft:water.swim";
inline constexpr auto uiClick = "qtcraft:ui.click";
inline constexpr auto ambientWind = "qtcraft:ambient.wind";
inline constexpr auto ambientCave = "qtcraft:ambient.cave";
inline constexpr auto ambientWater = "qtcraft:ambient.water";
// Rain falling around the listener and thunder after lightning (decision E-395).
inline constexpr auto ambientRain = "qtcraft:ambient.rain";
inline constexpr auto weatherThunder = "qtcraft:weather.thunder";
inline constexpr auto mobHurt = "qtcraft:mob.generic.hurt";
inline constexpr auto mobDeath = "qtcraft:mob.generic.death";
inline constexpr auto machineActive = "qtcraft:machine.furnace";
// Two liquids turning into stone, a bucket taking or pouring a liquid, a bottle catching a creature.
inline constexpr auto liquidHiss = "qtcraft:liquid.hiss";
inline constexpr auto bucketFill = "qtcraft:item.bucket.fill";
inline constexpr auto bucketEmpty = "qtcraft:item.bucket.empty";
inline constexpr auto bottleCatch = "qtcraft:item.bottle.catch";
// Armour put on or taken off, and a worn piece or tool breaking.
inline constexpr auto armourEquip = "qtcraft:item.armour.equip";
inline constexpr auto itemBreak = "qtcraft:item.break";
// Lava near the listener now and then.
inline constexpr auto lavaPop = "qtcraft:liquid.lava.pop";
// Circuits (decision E-336): a lever or button switching, a piston moving out and in, a Signal
// Torch burning out.
inline constexpr auto signalClick = "qtcraft:block.signal.click";
inline constexpr auto pistonOut = "qtcraft:block.piston.out";
inline constexpr auto pistonIn = "qtcraft:block.piston.in";
inline constexpr auto torchFizz = "qtcraft:block.torch.fizz";
// A dispenser or dropper ejecting an item, and one with nothing to eject or no room (decision E-345).
inline constexpr auto dispenserDispense = "qtcraft:block.dispenser.dispense";
inline constexpr auto dispenserFail = "qtcraft:block.dispenser.fail";
// Flint and steel striking a fire (decision E-393).
inline constexpr auto flintAndSteel = "qtcraft:item.flint_and_steel.use";
// A collected experience orb and a level gained (decision E-398).
inline constexpr auto experienceOrb = "qtcraft:player.experience";
inline constexpr auto levelUp = "qtcraft:player.levelup";
// A launcher drawn and shooting (decision E-425); projectiles name their own sounds.
inline constexpr auto bowDraw = "qtcraft:item.bow.draw";
inline constexpr auto bowShoot = "qtcraft:item.bow.shoot";
// An explosive lit (its fuse hisses) and an explosion (decision E-604).
inline constexpr auto explosivePrimed = "qtcraft:entity.explosive.primed";
inline constexpr auto explosion = "qtcraft:entity.explosive.explode";
// Material of blocks that do not name one and are not obviously wood, earth, glass or plants.
inline constexpr auto defaultMaterial = "qtcraft:stone";

// "break", "place", "step" or "hit".
QString actionName(QcBlockSoundAction action);
// The material a block without "sound" gets: none for invisible blocks and liquids, otherwise
// derived from its tool, shape and render layer.
QString defaultMaterialFor(const QcBlockDefinition &block);
// Sound of an action on a block; empty when the block makes no sound.
QString blockSound(const QcBlockDefinition &block, QcBlockSoundAction action);

} // namespace QcSoundKeys
