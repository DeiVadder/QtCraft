// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/mobs/QcMobManager.h"

#include <QList>

#include <optional>

/*! \brief The damage of crammed cells (game rule maxEntityCramming, decision E-401): creatures in a
    cell with more than the limit are hurt while it still holds more (counting those that died this
    tick), each only in every \c {phases}-th tick, staggered by its id.

    QcServerCreatures adds the sounds and the loot; the measurements call it on the creatures
    alone. */
namespace QcMobCramming {

/*! Damage of one cramming hit, as in the reference. Like any hit it respects the creature's
    invulnerability after a hurt (QcHurtWindow, game rule mobInvulnerabilityTicks): inside the
    window it does nothing unless it is stronger than the strongest hit of the window. */
inline constexpr double damage = 6.0;
/*! A crammed creature is hurt only in every this many ticks, staggered by its id: the reference's
    chance of one in four per tick, made deterministic. Deaths spread over a few ticks, so a crowd
    shrinks to about the limit instead of dying all at once. */
inline constexpr int phases = 4;

/*! \brief A creature cramming hurt: its kind, where it stood and its drops when it died. */
struct Hurt
{
    /*! Its entity id. */
    quint32 id = 0;
    /*! Its creature kind. */
    int type = 0;
    /*! Where it stood. */
    QcVec3 position;
    /*! Its drops when it died; none while it lives. */
    std::optional<QList<QcItemDrop>> drops;
};

/*! Hurts the creatures of \a mobs crammed beyond \a limit whose turn it is in tick \a tick (the
    crowd as the last creature tick counted it) and returns them. */
QList<Hurt> apply(QcMobManager &mobs, int limit, quint64 tick);

} // namespace QcMobCramming
