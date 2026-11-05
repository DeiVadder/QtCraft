// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcMessages.h"
#include "server/gameplay/QcArmourStandManager.h"

#include <optional>

class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

/*! \brief What players do to armour stands (decision E-238): placing one from its item, putting
    armour and the held item on or taking them off, and breaking it (two quick hits) with its
    drops. */
class QcServerStands
{
public:
    /*! Stands are turned to face the player in steps of this many degrees. */
    static constexpr float yawStep = 45.0f;

    QcServerStands(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    /*! Places a stand from the held item of \a client on top of the block \a message clicked.
        Returns \c false when the item places no stand. A refused placement (no room, too far,
        limit) still returns true. */
    bool place(QcServerClient &client, const QcMsg::UseItem &message);
    /*! Handles a right click of \a client on the stand \a id. Returns \c false when \a id is no
        stand. */
    bool use(QcServerClient &client, quint32 id);
    /*! Handles a hit of \a client on the stand \a id (none in adventure mode). Returns
        \c false when \a id is no stand. */
    bool hit(QcServerClient &client, quint32 id);
    /*! The stands' hit timers in a simulated tick (and stands in the void go). */
    void tick();
    /*! Removes the stand \a id; with \a drops its item, armour and held item fall where it
        stood. */
    void breakStand(quint32 id, bool drops);
    /*! Adds \a stand (commands, world setup); its id is assigned here. Returns it, or null when
        the limit is reached. */
    QcArmourStand *spawn(QcArmourStand stand);
    /*! Drops the armour and held item \a before had and \a after no longer has (a command
        replaced or cleared them) where the stand stands. */
    void dropReplaced(const QcArmourStand &before, const QcArmourStand &after);
    /*! Everyone who sees \a stand gets it again. */
    void changed(const QcArmourStand &stand);

    /*! Returns the equipment slot of the body part at \a height metres above the feet of a
        full-size stand. */
    static QcEquipmentSlot slotAtHeight(double height);
    /*! Returns the yaw a stand placed by a player looking along \a playerYaw gets: facing the
        player, in yawStep steps. */
    static float facingYaw(float playerYaw);

private:
    // Where the player's view meets the stand's body, as a height on a full-size stand; nothing when it misses.
    [[nodiscard]] std::optional<double> aimedHeight(const QcServerClient &client, const QcArmourStand &stand) const;
    // Swaps the held stack with what the stand wears in `slot` (or holds, without a slot).
    void swapWithHand(QcServerClient &client, QcArmourStand &stand, std::optional<QcEquipmentSlot> slot);
    bool isFreeForStand(QcBlockPos feet) const;
    bool canReach(const QcServerClient &client, const QcArmourStand &stand) const;

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
