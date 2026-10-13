#pragma once

#include "QcMobManager.h"

// Rules that keep the creatures of QcMobManager in the world or take them out: despawning and
// leaving blocks a creature stands in.
namespace QcMobUpkeep {

// Whether the creature leaves the world this tick: in the void (decision E-288), far from every
// player, or a night creature in daylight. Nobody is far away while nobody plays (decision E-353).
bool despawns(const QcMobManager::Mob &mob, const QcMobDefinition &definition, const QList<QcMobManager::PlayerView> &players,
              bool night);

// Moves a walker whose body overlaps blocks to the nearest clear place (QcBodyFit::clearFeet,
// decision E-352). With nowhere to go it stays and suffocates (QcServerCombat::applySuffocation).
void leaveBlocks(QcMobManager::Mob &mob, const QcMobDefinition &definition, const QcBlockAccess &world);

} // namespace QcMobUpkeep
