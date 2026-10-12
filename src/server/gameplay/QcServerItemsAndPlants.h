#pragma once

#include "QcItemEntityManager.h"

#include <QHash>
#include <QSet>

class QcServerBlocks;
class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
struct QcServerState;

// The per-tick life of dropped items (players pick them up, despawning saplings plant themselves,
// seeds lying on free soil plant themselves) and of plants (growing saplings, random ticks of plants
// that grow upwards like sugar cane, of crops and soil).
class QcServerItemsAndPlants
{
public:
    QcServerItemsAndPlants(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks, QcServerInventory &inventory);

    // Dropped items: pick-up, merging, despawning (saplings plant themselves).
    void tickItems();
    // Growing saplings and plants with random ticks (unless the game rule randomTicks is off).
    void tickPlants();

    // Seeds (items placing a crop with `plantChance`) plant themselves once every this many ticks
    // with that chance when they lie on soil with air above (decision E-294).
    static constexpr int seedCheckTicks = 20;

private:
    // Puts the stack into the inventory of the playing player with this entity id; returns what
    // did not fit and remembers players whose inventory changed.
    int giveToPlayer(quint32 entityId, QcItemStack stack, QSet<QcServerClient *> &changed);
    // Puts a despawning sapling item into the ground when there is room.
    void tryPlant(const QcItemEntityManager::Item &item);
    // Lets resting seed items on free soil plant one of their stack.
    void plantSeeds();
    // Where a resting seed item plants this check: its own cell or a free one around it (each
    // rolling the chance), none of the cells already `planned`.
    std::optional<QcBlockPos> seedTarget(const QcItemEntityManager::Item &item, QcBlockId crop, const QSet<QcBlockPos> &planned);
    // Soil at `ground` with air above where `crop` may stand.
    bool isFreeSoil(QcBlockPos ground, QcBlockId crop) const;

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerBlocks &m_blocks;
    QcServerInventory &m_inventory;
    quint64 m_itemTicks = 0;
    // Crop each self-planting seed item places.
    QHash<QcItemId, QcBlockId> m_seedCrops;
};
