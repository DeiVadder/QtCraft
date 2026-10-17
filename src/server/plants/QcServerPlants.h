#pragma once

#include "common/content/definitions/QcItemDefinition.h"
#include "common/core/QcBlockFace.h"
#include "server/items/QcItemEntityManager.h"

#include <QHash>
#include <QSet>

class QcServerBlocks;
class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

// Plants in the server tick: growing saplings and decaying leaves (QcPlantManager), random ticks of
// plants that grow upwards like sugar cane, of crops and soil (QcPlantGrowth), items that plant
// themselves (despawning saplings, seeds lying on free soil; decision E-294), and players tilling
// soil with a hoe (decision E-290) and making paths with a shovel (decision E-406).
class QcServerPlants
{
public:
    // Seeds (items placing a crop with `plantChance`) plant themselves once every this many ticks
    // with that chance when they lie on soil with air above (decision E-294).
    static constexpr int seedCheckTicks = 20;

    QcServerPlants(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks, QcServerInventory &inventory);

    // Growing saplings, decaying leaves and random ticks (unless the game rule randomTicks is off).
    void tick();
    // Items in the item phase of a tick: despawning saplings and, every seedCheckTicks, resting
    // seeds plant themselves.
    void plantItems(const QList<QcItemEntity> &despawned);
    // Tills the clicked block when the held item `tills` and the block has `tillsInto`, on any face
    // but the bottom, with air above, within reach. False when the click is no tilling (then other
    // uses of the item may follow).
    bool till(QcServerClient &client, QcBlockPos target, QcBlockFace face, const QcItemDefinition *held);
    // Flattens the clicked block into a path when the held item `flattens` and the block has
    // `flattensInto` (decision E-406): only on its top face, with air above, within reach. False when
    // the click is no flattening.
    bool flatten(QcServerClient &client, QcBlockPos target, QcBlockFace face, const QcItemDefinition *held);

private:
    // Puts a despawning sapling item into the ground when there is room.
    void tryPlant(const QcItemEntity &item);
    // Lets resting seed items on free soil plant one of their stack.
    void plantSeeds();
    // Where a resting seed item plants this check: its own cell or a free one around it (each
    // rolling the chance), none of the cells already `planned`.
    std::optional<QcBlockPos> seedTarget(const QcItemEntity &item, QcBlockId crop, const QSet<QcBlockPos> &planned);
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
