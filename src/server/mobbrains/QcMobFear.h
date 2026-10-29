// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/mobbrains/QcMobBrain.h"

#include <optional>

// What frightens a creature away (decision E-480): a shy one (QcMobDefinition::shy, the ocelot)
// flees from players who come close unless they hold its food and move slowly; one scared of cats
// (QcMobDefinition::scaredOfCats) flees from creatures that frighten (cats).
namespace QcMobFear {

// How close a creature that frightens (a cat) scares one that is scared of cats, in metres.
inline constexpr double catDistance = 6.0;

// The horizontal direction away from the nearest thing that frightens `mob` now (normalized); none
// when nothing does.
std::optional<QcVec3> away(const QcCreature &mob, const QcMobDefinition &definition, const QcMobBrain::Context &context);
// Whether a player calms a shy creature: holding one of its calm items and moving no faster than
// its calm speed.
bool calms(const QcMobManager::PlayerView &player, const QcShyDefinition &shy, const QcContentRegistry &content);

} // namespace QcMobFear
