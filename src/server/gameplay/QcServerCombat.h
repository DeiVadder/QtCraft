#pragma once

#include "QcBlockDefinition.h"
#include "QcVec3.h"

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

    void attack(QcServerClient &client, quint32 entityId);
    // Drops the loot of a creature that died at `position`, each drop by its chance (also burn
    // deaths, QcBodyBurning).
    static void dropLoot(QcServerState &state, const QList<QcItemDrop> &drops, const QcVec3 &position);

private:

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
