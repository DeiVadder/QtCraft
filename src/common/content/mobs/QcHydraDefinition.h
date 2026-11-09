// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

/*! \brief The three-headed flying boss of the Nether (the charred hydra, decisions E-1125 to
    E-1129; the role model's wither), from its data (\c {hydra}).

    \list
    \li It is summoned when a player places the last of three \c {summonHead} blocks on a T of
        \c {summonBody} blocks (QcHydraSummoning). It then charges for \c {chargeSeconds}: it
        neither moves nor fights nor takes damage, starts at chargeHealthShare of its health and
        heals \c {chargeHeal} points every chargeHealSeconds; at the end comes an explosion of
        \c {blastPower} and \c {summonSound} for every player of its dimension.
    \li Its main head shoots at its target every \c {attackSeconds} within \c {attackRange}
        metres: a \c {skull} at \c {skullSpeed} m/s, by the chance \c {chargedChance} a
        \c {chargedSkull} at \c {chargedSkullSpeed} m/s (slow, it breaks hard blocks). The side
        heads choose targets of their own and fire skulls too (QcHydraBrain).
    \li At or below \c {armourShare} of its health it wears its armour: projectiles that stick in
        blocks and hurt (arrows, QcHydraRules::shields) bounce off and it comes down to its
        target's height instead of keeping hoverAbove metres over it.
    \li It heals \c {regeneration} points a second; a fresh hurt breaks the blocks around its
        body a second later under the rule mobGriefing, with \c {breakSound}.
    \endlist

    Server side only. \qcfields */
struct QcHydraDefinition
{
    QStringList summonBody;
    QString summonHead;
    double chargeSeconds = 11.0;
    double chargeHeal = 10.0;
    double blastPower = 7.0;
    QString summonSound;
    double regeneration = 1.0;
    double armourShare = 0.5;
    double hoverAbove = 5.0;
    double attackRange = 20.0;
    double attackSeconds = 2.0;
    QString skull;
    QString chargedSkull;
    double skullSpeed = 16.0;
    double chargedSkullSpeed = 5.0;
    double chargedChance = 0.001;
    QString breakSound;

    /*! The share of its health a summoned one starts its charge with (the role model's third). */
    static constexpr double chargeHealthShare = 1.0 / 3.0;
    /*! Seconds between two heals of the charge (the role model's 10 ticks). */
    static constexpr double chargeHealSeconds = 0.5;

    /*! Returns whether a creature of this kind with \a health of \a maxHealth wears its armour. */
    [[nodiscard]] bool armouredAt(double health, double maxHealth) const { return health <= maxHealth * armourShare; }

    bool operator==(const QcHydraDefinition &) const = default;
};
