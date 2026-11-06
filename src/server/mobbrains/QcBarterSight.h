// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/mobbrains/QcMobBrain.h"

/*! \brief Whom a creature that barters (QcMobDefinition::barter, decision E-1081) sees as a foe
    without being provoked: a player who wears none of its QcBarterDefinition::sparedBy. */
namespace QcBarterSight {
/*! Returns whether \a player wears one of the items of \a barter's \c {sparedBy} (names in
    \a content). */
bool spares(const QcMobManager::PlayerView &player, const QcBarterDefinition &barter, const QcContentRegistry &content);
/*! Returns the foe of \a mob of the kind \a definition in the tick of \a context: when it
    \a looks, the nearest valid player it sees within its sight range who is not spared; between
    its looks the player it chases while he is still not spared and within its sight range; nullptr
    for none. */
const QcMobManager::PlayerView *foe(const QcCreature &mob, const QcMobDefinition &definition, const QcMobBrain::Context &context, bool looks);
} // namespace QcBarterSight
