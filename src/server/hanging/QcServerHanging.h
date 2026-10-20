#pragma once

#include "common/protocol/QcMessages.h"
#include "server/hanging/QcHangingManager.h"

class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

// What players and the world do to item frames and paintings (decisions E-790 to E-792): hanging
// one from its item on a block face, putting an item into a frame and turning it, hitting (a
// frame's item drops first, then the frame) and dropping them when their wall or room goes.
// Holds no state of its own, so the server makes one where it needs it.
class QcServerHanging
{
public:
    QcServerHanging(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    // Hangs a frame or painting from the held item on the clicked face; false when the item hangs
    // nothing. A refused placement (no room, too far, limit, adventure mode) still returns true.
    bool place(QcServerClient &client, const QcMsg::UseItem &message);
    // Right click on a frame or painting; false when `id` is none.
    bool use(QcServerClient &client, quint32 id);
    // A hit (none in adventure mode); false when `id` is no frame or painting.
    bool hit(QcServerClient &client, quint32 id);
    // Frames and paintings whose wall or room went fall off with their drops; checked after a block
    // change next to them and once every QcHangingManager::supportCheckTicks ticks.
    void tick();
    // Adds one (placing, world setup, tests); its id and position are set here and comparators
    // near it look again. Null at the limit.
    static QcHangingEntity *spawn(QcServerState &state, QcHangingEntity hanging);
    // Removes it; with `drops` its item and what it held fall where it hung.
    void remove(quint32 id, bool drops);

private:
    bool canReach(const QcServerClient &client, const QcHangingEntity &hanging) const;
    // Puts one of the held stack into an empty frame, or turns its item an eighth further.
    void useFrame(QcServerClient &client, quint32 id);
    void drop(QcItemStack stack, const QcHangingEntity &hanging);
    // The frame's look changed: clients get it again, comparators read it again.
    void changed(const QcHangingEntity &hanging);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
