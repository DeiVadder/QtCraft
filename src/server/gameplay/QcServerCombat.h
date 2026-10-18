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

    // A blow or a hit on a creature: from where, and whether a player dealt it (only then it gives
    // experience).
    struct Blow
    {
        quint32 creature = 0;
        double damage = 0.0;
        QcVec3 from;
        bool byPlayer = false;
        // Strength of the push away from `from`; 1 is a blow's (decision E-427).
        double knockback = 1.0;
    };

    void attack(QcServerClient &client, quint32 entityId);
    // Deals `blow` with the creature's sounds, its loot and experience when it dies; false when the
    // creature was not hurt (unknown, or invulnerable for a moment after the last hit and the blow
    // no stronger than the strongest of that moment, decision E-429).
    static bool hurtCreature(QcServerState &state, QcServerSounds &sounds, const Blow &blow);
    // Drops the loot of a creature that died at `position`, each drop by its chance (also burn
    // deaths, QcBodyBurning).
    static void dropLoot(QcServerState &state, const QList<QcItemDrop> &drops, const QcVec3 &position);

private:

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
