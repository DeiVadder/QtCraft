#pragma once

#include "QcItemEntityManager.h"

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
    // Puts the stack into the inventory of the playing player with this entity id; returns what
    // did not fit and remembers players whose inventory changed.
    int giveToPlayer(quint32 entityId, QcItemStack stack, QSet<QcServerClient *> &changed);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
