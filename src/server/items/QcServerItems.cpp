// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/items/QcServerItems.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcServerInventory.h"

QcServerItems::QcServerItems(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
{
}

QList<QcItemEntity> QcServerItems::tick()
{
    const double dt = QcPhysics::tickDuration;
    QList<QcItemEntityManager::PlayerView> players;
    for (const auto &client : m_state.clients()) {
        // Spectators pick up nothing (decision E-730).
        if (client->hasBody()) {
            players.append({client->id, client->motion.position, true});
        }
    }

    QSet<QcServerClient *> changed;
    const QList<QcItemEntity> despawned = m_state.entities().items().tick(
        m_state.world(), players, dt,
        {[this](const QcVec3 &position) { return m_state.isLoadedAt(QcPhysics::blockContaining(position)); },
         [this, &changed](quint32 entityId, const QcItemStack &stack) {
             const Given given = giveToPlayer(entityId, stack);
             if (given.taker) {
                 changed.insert(given.taker);
             }
             return given.left;
         },
         [this] { return m_state.entities().newId(); }});

    for (QcServerClient *client : std::as_const(changed)) {
        m_inventory.send(*client);
    }
    return despawned;
}

QcServerItems::Given QcServerItems::giveToPlayer(quint32 entityId, const QcItemStack &stack)
{
    for (const auto &client : m_state.clients()) {
        if (client->id != entityId || client->state != QcServerClient::State::Playing) {
            continue;
        }

        const int left = client->inventory.add(stack, m_state.content());
        if (left >= stack.count) {
            return {left, nullptr};
        }
        m_sounds.play(QString::fromLatin1(QcSoundKeys::itemPickup), client->motion.position, 0.6f);
        return {left, client.get()};
    }
    return {stack.count, nullptr};
}
