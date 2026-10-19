#pragma once

#include "server/items/QcItemEntityManager.h"

#include <QSet>

class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

// Dropped items in the server tick: they move, merge and despawn (QcItemEntityManager), and players
// pick them up.
class QcServerItems
{
public:
    QcServerItems(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    // Returns the items that despawned this tick (saplings among them may plant themselves).
    QList<QcItemEntity> tick();

private:
    // What did not fit of a stack given to a player, and the player when his inventory took some.
    struct Given
    {
        int left = 0;
        QcServerClient *taker = nullptr;
    };

    // Puts the stack into the inventory of the playing player with this entity id.
    [[nodiscard]] Given giveToPlayer(quint32 entityId, QcItemStack stack);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
