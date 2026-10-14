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

private:
    void dropLoot(const QList<QcItemDrop> &drops, const QcVec3 &position);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
