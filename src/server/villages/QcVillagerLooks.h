#pragma once

#include "QcAppearance.h"

class QcContentRegistry;
struct QcVillagerRecord;

// What a villager looks like (decision E-411): the look of its village style (the default look of
// villagers.json for a style without one), its profession's outfit (the style's outfit with the
// profession's layers, QcVillagerOutfits) and hat in place of the style's hair, and one of each of
// the style's hair styles and colours drawn from the villager's seed. The server sends it with the
// villager (QcMsg::EntitySpawn::appearance).
namespace QcVillagerLooks {

QcAppearance appearanceOf(const QcContentRegistry &content, const QcVillagerRecord &villager);

} // namespace QcVillagerLooks
