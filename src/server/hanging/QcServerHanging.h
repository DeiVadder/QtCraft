// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcMessages.h"
#include "server/hanging/QcHangingManager.h"

class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

/*! \brief What players and the world do to item frames and paintings (decisions E-790 to E-792):
    hanging one from its item on a block face, putting an item into a frame and turning it, hitting
    (a frame's item drops first, then the frame) and dropping them when their wall or room goes.

    Holds no state of its own, so the server makes one where it needs it. */
class QcServerHanging
{
public:
    QcServerHanging(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    /*! Hangs a frame or painting from the held item of \a client on the face \a message clicked.
        Returns \c false when the item hangs nothing. A refused placement (no room, too far,
        limit, adventure mode) still returns true. */
    bool place(QcServerClient &client, const QcMsg::UseItem &message);
    /*! Handles a right click of \a client on the frame or painting \a id. Returns \c false when
        \a id is none. */
    bool use(QcServerClient &client, quint32 id);
    /*! Handles a hit of \a client on the frame or painting \a id (none in adventure mode).
        Returns \c false when \a id is no frame or painting. */
    bool hit(QcServerClient &client, quint32 id);
    /*! Frames and paintings whose wall or room went fall off with their drops; checked after a
        block change next to them and once every QcHangingManager::supportCheckTicks ticks. */
    void tick();
    /*! Adds \a hanging to \a state (placing, world setup, tests); its id and position are set
        here and comparators near it look again. Returns it, or null at the limit. */
    static QcHangingEntity *spawn(QcServerState &state, QcHangingEntity hanging);
    /*! Removes the frame or painting \a id; with \a drops its item and what it held fall where
        it hung. */
    void remove(quint32 id, bool drops);

private:
    bool canReach(const QcServerClient &client, const QcHangingEntity &hanging) const;
    // Puts one of the held stack into an empty frame, or turns its item an eighth further.
    void useFrame(QcServerClient &client, quint32 id);
    void drop(const QcItemStack &stack, const QcHangingEntity &hanging);
    // The frame's look changed: clients get it again, comparators read it again.
    void changed(const QcHangingEntity &hanging);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
