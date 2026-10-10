#pragma once

#include "QcArmourStandManager.h"

#include <QString>
#include <QStringList>

#include <optional>

class QcServerInventory;
class QcServerStands;
struct QcServerClient;
struct QcServerState;

// Chat commands for armour stands and armour (decision E-239), called by QcChatCommands:
//   /armorstand <nearest|looking|id> <property> [values]
//   /summon <stand item> [property=value[,value…] …]
//   /equip <item|set|clear>
// The properties and their checks are QcStandProperties; "info" lists them.
class QcStandCommands
{
public:
    // How far `nearest` and ids reach from the player.
    static constexpr double targetRange = 16.0;

    QcStandCommands(QcServerState &state, QcServerStands &stands, QcServerInventory &inventory);

    void armorStand(QcServerClient &client, const QStringList &words);
    // False when `words[1]` names no item that places a stand (then it is a creature).
    bool summon(QcServerClient &client, const QStringList &words);
    void equip(QcServerClient &client, const QStringList &words);


private:
    QcArmourStandManager::Stand *target(const QcServerClient &client, const QString &word);
    QcArmourStandManager::Stand *standInView(const QcServerClient &client);
    // Where a summoned stand stands: three metres in front of the player, on the ground below.
    std::optional<QcVec3> summonPosition(const QcServerClient &client) const;
    static QString describe(const QcArmourStandManager::Stand &stand);

    QcServerState &m_state;
    QcServerStands &m_stands;
    QcServerInventory &m_inventory;
};
