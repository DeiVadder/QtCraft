#pragma once

#include "QcArmourStandManager.h"
#include "QcMessages.h"

#include <optional>

class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
struct QcServerState;

// What players do to armour stands (decision E-238): placing one from its item, putting armour and
// the held item on or taking them off, and breaking it (two quick hits) with its drops.
class QcServerStands
{
public:
    // Stands are turned to face the player in steps of this many degrees.
    static constexpr float yawStep = 45.0f;

    QcServerStands(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    // Places a stand from the held item on top of the clicked block; false when the item places no
    // stand. A refused placement (no room, too far, limit) still returns true.
    bool place(QcServerClient &client, const QcMsg::UseItem &message);
    // Right click on a stand; false when `id` is no stand.
    bool use(QcServerClient &client, quint32 id);
    // A hit on a stand; false when `id` is no stand.
    bool hit(QcServerClient &client, quint32 id);
    // Removes the stand; with `drops` its item, armour and held item fall where it stood.
    void breakStand(quint32 id, bool drops);
    // Adds a stand (commands, world setup); its id is assigned here. Null when the limit is reached.
    QcArmourStandManager::Stand *spawn(QcArmourStandManager::Stand stand);
    // Drops the armour and held item `before` had and `after` no longer has (a command replaced or
    // cleared them) where the stand stands.
    void dropReplaced(const QcArmourStandManager::Stand &before, const QcArmourStandManager::Stand &after);
    // Everyone who sees the stand gets it again.
    void changed(const QcArmourStandManager::Stand &stand);

    // The equipment slot of the body part at `height` metres above the feet of a full-size stand.
    static QcEquipmentSlot slotAtHeight(double height);
    // The yaw a stand placed by a player looking along `playerYaw` gets: facing the player, in yawStep steps.
    static float facingYaw(float playerYaw);

private:
    // Where the player's view meets the stand's body, as a height on a full-size stand; nothing when it misses.
    std::optional<double> aimedHeight(const QcServerClient &client, const QcArmourStandManager::Stand &stand) const;
    // Swaps the held stack with what the stand wears in `slot` (or holds, without a slot).
    void swapWithHand(QcServerClient &client, QcArmourStandManager::Stand &stand, std::optional<QcEquipmentSlot> slot);
    bool isFreeForStand(QcBlockPos feet) const;
    bool canReach(const QcServerClient &client, const QcArmourStandManager::Stand &stand) const;

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
