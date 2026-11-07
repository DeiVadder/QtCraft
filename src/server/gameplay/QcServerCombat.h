// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/core/QcVec3.h"

#include <QList>

class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

/*! \brief Players attacking creatures (decision E-367): reach and sight, the attack cooldown, the
    damage of the held item and its wear, the creature's sounds and the loot it drops when it dies.

    Armour stands take hits in QcServerStands. */
class QcServerCombat
{
public:
    /*! Melee reach of players, from the eyes to the target's centre. */
    static constexpr double attackReach = 4.0;
    /*! Seconds a player waits between two attacks. */
    static constexpr double playerAttackCooldown = 0.4;
    /*! Blows' worth of push of a sprinting player's attack (decision E-971): the reference adds
        a level of knockback, which pushes about twice as far. */
    static constexpr double sprintKnockback = 2.0;

    QcServerCombat(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    /*! \brief A blow or a hit on a creature: from where, and the player who dealt it (only then
        it gives experience and provokes a neutral creature, decision E-460, and villages and
        golems remember them: QcGolemGrudges, decision E-471). */
    struct Blow
    {
        /*! Entity id of the creature hit. */
        quint32 creature = 0;
        /*! The damage dealt. */
        double damage = 0.0;
        /*! Where the blow came from. */
        QcVec3 from;
        /*! Entity id of the player; 0 for none (a creature's blow or shot). */
        quint32 attacker = 0;
        /*! Strength of the push away from \c {from}; 1 is a blow's (decision E-427). */
        double knockback = 1.0;
        /*! Times a blow's usual lift (a golem's throws upwards, decision E-471). */
        double lift = 1.0;
    };

    /*! Handles an attack of the player \a client on the entity \a entityId; a sprinting
        player pushes harder (sprintKnockback). */
    void attack(QcServerClient &client, quint32 entityId);
    /*! Deals \a blow in \a state with the creature's \a sounds, its loot and experience when it
        dies. Returns \c false when the creature was not hurt (unknown, or invulnerable for a
        moment after the last hit and the blow no stronger than the strongest of that moment,
        decision E-429). */
    static bool hurtCreature(QcServerState &state, QcServerSounds &sounds, const Blow &blow);
    /*! Wounds the creature \a id of \a state by \a amount without a blow (burning, drying,
        drowning): the hurt sound of \a sounds only when the damage takes as a fresh hit (not
        while the creature is still invulnerable from the last, decision E-429), the death sound
        and the loot when it dies. Nothing for an unknown creature. */
    static void woundCreature(QcServerState &state, QcServerSounds &sounds, quint32 id, double amount);
    /*! Drops the loot \a drops of a creature of \a state that died at \a position, each drop by
        its chance (also burn deaths, QcBodyBurning), a drop with a loot table as one draw of it
        (decision E-1106); those for a player's kill only with \a byPlayer. */
    static void dropLoot(QcServerState &state, const QList<QcItemDrop> &drops, const QcVec3 &position, bool byPlayer = false);
    /*! Tells every player of \a state that the creature named \a name died (decision E-866);
        nothing for no name. */
    static void tellNamedDeath(QcServerState &state, const QString &name);

private:

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
