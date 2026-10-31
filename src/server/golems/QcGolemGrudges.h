// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

class QcServerWorld;
struct QcCreature;

/*! \brief Who iron golems attack among the players (decision E-471), a light form of the reference
    game's popularity: a player who hurts a villager of a village, or a golem standing in a
    village's area, gives that village a grudge against them for the village style's \c
    {grudgeSeconds} (QcVillageGolemSettings), renewed by every hit; the village's golems attack them
    on sight while it lasts (QcGolemBrain::enemy).

    A golem hurt by a player is also angry with them for angerSeconds wherever it stands. Golems
    a player built never attack players. Grudges and anger are not saved. */
namespace QcGolemGrudges {

/*! Seconds a golem hurt by a player stays angry with them. */
inline constexpr double angerSeconds = 30.0;

/*! A player (entity id \a player, called \a name) dealt \a mob a blow at \a worldTime (called
    before the blow, so a killing blow counts too): its golem anger (by entity id) and its
    village's grudge in \a world (by name, so it outlasts leaving and joining again). */
void noteBlow(QcServerWorld &world, QcCreature &mob, quint32 player, const QString &name, double worldTime);

} // namespace QcGolemGrudges
