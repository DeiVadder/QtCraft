#pragma once

#include "QcItemEntityManager.h"

#include <QSet>

class QcServerBlocks;
class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
struct QcServerState;

// The per-tick life of dropped items (players pick them up, despawning saplings plant themselves)
// and of plants (growing saplings, plants that grow upwards like sugar cane).
class QcServerItemsAndPlants
{
public:
    QcServerItemsAndPlants(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks, QcServerInventory &inventory);

    void tick();

private:
    // Puts the stack into the inventory of the playing player with this entity id; returns what
    // did not fit and remembers players whose inventory changed.
    int giveToPlayer(quint32 entityId, QcItemStack stack, QSet<QcServerClient *> &changed);
    // Puts a despawning sapling item into the ground when there is room.
    void tryPlant(const QcItemEntityManager::Item &item);
    void tickPlants(double dt);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerBlocks &m_blocks;
    QcServerInventory &m_inventory;
};
