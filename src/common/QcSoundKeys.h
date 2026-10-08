#pragma once

#include "QcBlockDefinition.h"

#include <QString>

// Names of the sounds the game plays for blocks, creatures and the player. Blocks name a sound
// material; the sound of an action is "<namespace>:block.<material>.<action>" unless the block
// gives an explicit key for that action.
namespace QcSoundKeys {

inline constexpr auto playerHurt = "qtcraft:player.hurt";
inline constexpr auto playerDeath = "qtcraft:player.death";
inline constexpr auto playerEat = "qtcraft:player.eat";
inline constexpr auto itemPickup = "qtcraft:item.pickup";
inline constexpr auto waterSplash = "qtcraft:water.splash";
inline constexpr auto waterSwim = "qtcraft:water.swim";
inline constexpr auto uiClick = "qtcraft:ui.click";
inline constexpr auto ambientWind = "qtcraft:ambient.wind";
inline constexpr auto ambientCave = "qtcraft:ambient.cave";
inline constexpr auto ambientWater = "qtcraft:ambient.water";
inline constexpr auto mobHurt = "qtcraft:mob.generic.hurt";
inline constexpr auto mobDeath = "qtcraft:mob.generic.death";
inline constexpr auto machineActive = "qtcraft:machine.furnace";
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
