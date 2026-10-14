#pragma once

#include "QcItemEntityManager.h"

#include <QHash>
#include <QSet>

class QcServerBlocks;
class QcServerSounds;
struct QcServerState;

// Plants in the server tick: growing saplings and decaying leaves (QcPlantManager), random ticks of
// plants that grow upwards like sugar cane, of crops and soil (QcPlantGrowth), and items that plant
// themselves (despawning saplings, seeds lying on free soil; decision E-294).
class QcServerPlants
{
public:
    // Seeds (items placing a crop with `plantChance`) plant themselves once every this many ticks
    // with that chance when they lie on soil with air above (decision E-294).
    static constexpr int seedCheckTicks = 20;

    QcServerPlants(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks);

    // Growing saplings, decaying leaves and random ticks (unless the game rule randomTicks is off).
    void tick();
    // Items in the item phase of a tick: despawning saplings and, every seedCheckTicks, resting
    // seeds plant themselves.
    void plantItems(const QList<QcItemEntityManager::Item> &despawned);

private:
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
    quint64 m_itemTicks = 0;
    // Crop each self-planting seed item places.
    QHash<QcItemId, QcBlockId> m_seedCrops;
};
