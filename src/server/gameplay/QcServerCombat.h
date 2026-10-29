// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcVec3.h"

#include <QList>

class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

// Players attacking creatures (decision E-367): reach and sight, the attack cooldown, the damage of
// the held item and its wear, the creature's sounds and the loot it drops when it dies. Armour
// stands take hits in QcServerStands.
class QcServerCombat
{
public:
    // Melee reach of players, from the eyes to the target's centre.
    static constexpr double attackReach = 4.0;
    static constexpr double playerAttackCooldown = 0.4;

    QcServerCombat(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    // A blow or a hit on a creature: from where, and the player who dealt it (only then it gives
    // experience and provokes a neutral creature, decision E-460, and villages and golems remember
    // them: QcGolemGrudges, decision E-471).
    struct Blow
    {
        quint32 creature = 0;
        double damage = 0.0;
        QcVec3 from;
        // Entity id of the player; 0 for none (a creature's blow or shot).
        quint32 attacker = 0;
        // Strength of the push away from `from`; 1 is a blow's (decision E-427).
        double knockback = 1.0;
        // Times a blow's usual lift (a golem's throws upwards, decision E-471).
        double lift = 1.0;
    };

    void attack(QcServerClient &client, quint32 entityId);
    // Deals `blow` with the creature's sounds, its loot and experience when it dies; false when the
    // creature was not hurt (unknown, or invulnerable for a moment after the last hit and the blow
    // no stronger than the strongest of that moment, decision E-429).
    static bool hurtCreature(QcServerState &state, QcServerSounds &sounds, const Blow &blow);
    // Drops the loot of a creature that died at `position`, each drop by its chance (also burn
    // deaths, QcBodyBurning).
    static void dropLoot(QcServerState &state, const QList<QcItemDrop> &drops, const QcVec3 &position);
    // Tells every player that the creature named `name` died (decision E-866); nothing for no name.
    static void tellNamedDeath(QcServerState &state, const QString &name);

private:

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
