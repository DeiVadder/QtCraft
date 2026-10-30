// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStringList>

class QcContentRegistry;

/*! \brief Joins the village files of all packs when the content is finished
    (QcContentRegistry::finish, decision E-421), in two steps around linking the creatures to
    their villager settings. */
namespace QcVillageContent {

/*! Makes the workstation blocks of the professions in \a registry the village settings'
    workstations (a block two professions name goes to the later one, with a warning), and gives
    every villager species all professions. A species whose "unemployed" is no profession without
    workstations is dropped (its creatures are then no villagers). Returns the warnings for
    unknown blocks, shared blocks and dropped species. */
QStringList resolveSpecies(QcContentRegistry &registry);

/*! Settles the styles of \a registry: of two styles naming a biome, or both being the fallback,
    the later registered one (QcVillageStyleDefinition::registered: by pack order, the last pack
    that defined or extended it) keeps it, with a warning. Warns about unknown biomes, villager
    creatures that are no villagers (the styles' and the settings') and unknown residents. An
    unknown golem creature is no warning: the rule rests until the content has it
    (QcVillageGolems::kind logs it). Returns the warnings. */
QStringList resolveStyles(QcContentRegistry &registry);

} // namespace QcVillageContent
