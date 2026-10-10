#include "QcServerItemsAndPlants.h"

#include "QcPhysics.h"
#include "QcServerBlocks.h"
#include "QcServerInventory.h"
#include "QcServerSounds.h"
#include "QcServerState.h"
#include "QcSoundKeys.h"

QcServerItemsAndPlants::QcServerItemsAndPlants(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks,
                                               QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_blocks(blocks)
    , m_inventory(inventory)
{
}

void QcServerItemsAndPlants::tick()
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

    tickPlants(dt);
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

void QcServerItemsAndPlants::tickPlants(double dt)
{
    const auto setBlock = [this](QcBlockPos pos, QcBlockId block) {
        m_blocks.setAndBroadcast(pos, block);
        return m_state.world.blockAt(pos) == block;
    };
    m_state.plants.tick(
        m_state.world, dt, [this](QcBlockPos pos) { return m_state.isLoadedAt(pos); }, setBlock,
        [this](QcBlockPos pos) { m_blocks.breakBlock(pos, true); });
    m_state.growth.tick(m_state.world, setBlock);
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
