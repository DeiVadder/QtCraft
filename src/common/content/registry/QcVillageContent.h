#pragma once

#include <QStringList>

class QcContentRegistry;

// Joins the village files of all packs when the content is finished (QcContentRegistry::finish,
// decision E-421), in two steps around linking the creatures to their villager settings.
namespace QcVillageContent {

// The workstation blocks of the professions become the village settings' workstations (a block two
// professions name goes to the later one, with a warning), and every villager species gets all
// professions. A species whose "unemployed" is no profession without workstations is dropped (its
// creatures are then no villagers). Warnings for unknown blocks, shared blocks and dropped species.
QStringList resolveSpecies(QcContentRegistry &registry);

// Settles the styles: of two styles naming a biome, or both being the fallback, the later
// registered one (QcVillageStyleDefinition::registered: by pack order, the last pack that defined
// or extended it) keeps it, with a warning. Warns about unknown biomes, villager creatures that are
// no villagers (the styles' and the settings') and unknown residents. An unknown golem creature is
// no warning: the rule rests until the content has it (QcVillageGolems::kind logs it).
QStringList resolveStyles(QcContentRegistry &registry);

} // namespace QcVillageContent
