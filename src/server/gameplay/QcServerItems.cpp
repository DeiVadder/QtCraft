#include "QcServerItems.h"

#include "QcPhysics.h"
#include "QcServerInventory.h"
#include "QcServerSounds.h"
#include "QcServerState.h"
#include "QcSoundKeys.h"

QcServerItems::QcServerItems(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
{
}

QList<QcItemEntityManager::Item> QcServerItems::tick()
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
    return despawned;
}

int QcServerItems::giveToPlayer(quint32 entityId, QcItemStack stack, QSet<QcServerClient *> &changed)
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
