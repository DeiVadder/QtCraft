#include "QcServerItemsAndPlants.h"

#include "QcBlockRules.h"
#include "QcPhysics.h"
#include "QcServerBlocks.h"
#include "QcServerInventory.h"
#include "QcServerSounds.h"
#include "QcServerState.h"
#include "QcSoundKeys.h"

#include <array>

QcServerItemsAndPlants::QcServerItemsAndPlants(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks,
                                               QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_blocks(blocks)
    , m_inventory(inventory)
{
    // Items that plant themselves: they place a crop with a plantChance (decision E-294).
    const QcContentRegistry &content = *m_state.content;
    for (QcItemId id = 1; content.isValidItem(id); ++id) {
        const std::optional<QcBlockId> crop = content.blockId(content.item(id).placesBlock);
        if (crop && content.block(*crop).crop.plantChance > 0.0) {
            m_seedCrops.insert(id, *crop);
        }
    }
}

void QcServerItemsAndPlants::tickItems()
{
    const double dt = QcPhysics::tickDuration;
    QList<QcItemEntityManager::PlayerView> players;
    for (const auto &client : m_state.clients) {
        if (client->state == QcServerClient::State::Playing) {
            players.append({client->entityId, client->motion.position, !client->dead});
        }
    }

    QSet<QcServerClient *> changed;
    const QList<QcItemEntityManager::Item> despawned = m_state.items.tick(
        m_state.world, players, dt,
        {[this](const QcVec3 &position) { return m_state.isLoadedAt(QcPhysics::blockContaining(position)); },
         [this, &changed](quint32 entityId, QcItemStack stack) { return giveToPlayer(entityId, stack, changed); },
         [this] { return m_state.newEntityId(); }});

    for (QcServerClient *client : std::as_const(changed)) {
        m_inventory.send(*client);
    }
    for (const QcItemEntityManager::Item &item : despawned) {
        tryPlant(item);
    }

    if (++m_itemTicks % seedCheckTicks == 0) {
        plantSeeds();
    }
}

void QcServerItemsAndPlants::plantSeeds()
{
    struct Planting
    {
        quint32 item = 0;
        QcBlockPos pos;
        QcBlockId crop = qcAirBlockId;
    };

    QList<Planting> plantings;
    // Cells planned in this check: two stacks never plant into one cell.
    QSet<QcBlockPos> planned;
    for (const QcItemEntityManager::Item &item : m_state.items.items()) {
        const auto crop = m_seedCrops.constFind(item.stack.item);
        if (item.dormant || !item.resting || crop == m_seedCrops.constEnd()) {
            continue;
        }

        if (const std::optional<QcBlockPos> target = seedTarget(item, *crop, planned)) {
            plantings.append({item.id, *target, *crop});
            planned.insert(*target);
        }
    }

    // Planted afterwards: taking items changes the item list.
    for (const Planting &planting : std::as_const(plantings)) {
        if (m_state.world.blockAt(planting.pos) != qcAirBlockId || !m_state.items.takeOne(planting.item)) {
            continue;
        }
        m_blocks.setAndBroadcast(planting.pos, planting.crop);
        m_sounds.playBlock(m_state.content->block(planting.crop), QcBlockSoundAction::Place, planting.pos);
    }
}

std::optional<QcBlockPos> QcServerItemsAndPlants::seedTarget(const QcItemEntityManager::Item &item, QcBlockId crop,
                                                             const QSet<QcBlockPos> &planned)
{
    // The item lies on the top of the block below its feet (farmland is lower than a full block).
    // Its own cell comes first; a stack whose cell is taken (or that keeps more seeds after
    // planting it) reaches the free soil around it within the merge radius, so merged drops still
    // replant a whole field. Each cell rolls its own chance; one seed per stack and check.
    static constexpr std::array<std::pair<int, int>, 9> cells{
        {{0, 0}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}}};
    const QcBlockPos ground = QcPhysics::blockContaining(item.motion.position - QcVec3{0.0, 0.1, 0.0});
    const double chance = m_state.content->block(crop).crop.plantChance;
    for (const auto &[dx, dz] : cells) {
        const QcBlockPos soil = ground.offset(dx, 0, dz);
        if (!planned.contains(soil.offset(0, 1, 0)) && isFreeSoil(soil, crop) && m_state.random.generateDouble() < chance) {
            return soil.offset(0, 1, 0);
        }
    }
    return std::nullopt;
}

bool QcServerItemsAndPlants::isFreeSoil(QcBlockPos ground, QcBlockId crop) const
{
    const QcBlockPos target = ground.offset(0, 1, 0);
    const std::optional<QcBlockId> soil = m_state.world.blockAt(ground);
    if (!soil || !m_state.content->isValidBlock(*soil) || !m_state.content->block(*soil).soil.isSoil()
        || m_state.world.blockAt(target) != qcAirBlockId || !target.isInsideWorldHeight()) {
        return false;
    }
    return QcBlockRules::isSupported(m_state.world, target, crop);
}

int QcServerItemsAndPlants::giveToPlayer(quint32 entityId, QcItemStack stack, QSet<QcServerClient *> &changed)
{
    for (const auto &client : m_state.clients) {
        if (client->entityId != entityId || client->state != QcServerClient::State::Playing) {
            continue;
        }

        const int left = client->inventory.add(stack.item, stack.count, *m_state.content, stack.damage);
        if (left < stack.count) {
            changed.insert(client.get());
            m_sounds.play(QString::fromLatin1(QcSoundKeys::itemPickup), client->motion.position, 0.6f);
        }
        return left;
    }
    return stack.count;
}

void QcServerItemsAndPlants::tickPlants()
{
    const double dt = QcPhysics::tickDuration;
    const auto setBlock = [this](QcBlockPos pos, QcBlockId block) {
        m_blocks.setAndBroadcast(pos, block);
        return m_state.world.blockAt(pos) == block;
    };
    m_state.plants.tick(
        m_state.world, dt, [this](QcBlockPos pos) { return m_state.isLoadedAt(pos); }, setBlock,
        [this](QcBlockPos pos) { m_blocks.breakBlock(pos, true); });
    if (m_state.info.randomTicks) {
        m_state.growth.tick(m_state.world, setBlock);
    }
}

void QcServerItemsAndPlants::tryPlant(const QcItemEntityManager::Item &item)
{
    const QcContentRegistry &content = *m_state.content;
    const QcItemDefinition &definition = content.item(item.stack.item);
    const std::optional<QcBlockId> sapling = content.blockId(definition.placesBlock);
    if (!sapling || !content.block(*sapling).sapling.isSapling()
        || m_state.random.generateDouble() >= content.block(*sapling).sapling.plantChance) {
        return;
    }

    const QcBlockPos pos = QcPhysics::blockContaining(item.motion.position + QcVec3{0.0, 0.05, 0.0});
    const std::optional<QcBlockId> existing = m_state.world.blockAt(pos);
    if (!existing || !pos.isInsideWorldHeight() || !content.block(*existing).replaceable || content.block(*existing).liquid
        || !m_state.plants.canPlant(*sapling, m_state.world.blockAt(pos.offset(0, -1, 0)))) {
        return;
    }

    m_blocks.setAndBroadcast(pos, *sapling);
    if (m_state.world.blockAt(pos) == *sapling) {
        m_state.plants.saplingPlaced(pos, *sapling);
    }
}
