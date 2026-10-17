#include "server/projectiles/QcPlayerThrows.h"

#include "common/physics/QcPhysics.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/mobs/QcArcherBrain.h"
#include "server/projectiles/QcServerProjectiles.h"

#include <cmath>

QcPlayerThrows::QcPlayerThrows(QcServerState &state, QcServerInventory &inventory, QcServerProjectiles &projectiles)
    : m_state(state)
    , m_inventory(inventory)
    , m_projectiles(projectiles)
{
}

bool QcPlayerThrows::throwHeld(QcServerClient &client)
{
    const QcItemDefinition *held = m_state.heldItem(client);
    if (client.dead || client.state != QcServerClient::State::Playing || !held || !held->throwable || held->projectile.isEmpty()) {
        return false;
    }

    const int slot = client.inventory.selectedHotbarSlot();
    const QcItemStack stack = client.inventory.slot(slot);
    const quint64 now = m_state.clock().tick();
    if (now < client.throwReady.value(stack.item, 0)) {
        return false;
    }

    const bool creative = client.rules().infiniteItems;
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 direction = QcProjectileFlight::direction(client.yaw, client.pitch);
    const QcVec3 velocity = QcArcherBrain::spread(direction * held->throwable->speed, held->throwable->inaccuracyDegrees, m_state.random());
    const QcProjectileManager::Launch shot{client.id, held->projectile, eye + direction * QcPlayerLaunchers::muzzle, velocity,
                                           creative ? QcProjectilePickup::Remove : QcProjectilePickup::Item};
    if (!m_projectiles.launch(shot)) {
        return false;
    }

    client.swung = true;
    client.throwReady.insert(stack.item, now + quint64(std::lround(held->throwable->cooldownSeconds / QcPhysics::tickDuration)));
    if (!creative) {
        client.inventory.setSlot(slot, stack.count > 1 ? QcItemStack{stack.item, stack.count - 1, stack.damage} : QcItemStack{});
        m_inventory.send(client);
    }
    return true;
}
