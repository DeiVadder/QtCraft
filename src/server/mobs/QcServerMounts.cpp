// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/mobs/QcServerMounts.h"

#include "common/content/registry/QcSoundKeys.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerClient.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcSafeSpot.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/gameplay/QcServerInventory.h"

#include <algorithm>
#include <utility>

namespace {

// Height of the seat above the feet of a kind; a kind without one seats nobody.
double seatOf(const QcMobDefinition &definition)
{
    return definition.ride ? definition.ride->seat : 0.0;
}

} // namespace

QcServerMounts::QcServerMounts(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
{
}

bool QcServerMounts::use(QcServerClient &client, quint32 id)
{
    QcMobManager &mobs = m_state.entities().creatures();
    QcCreature *mob = mobs.find(id);
    if (!mob || mob->dormant || client.dead || !m_state.content().mobs()[mob->type].ride) {
        return false;
    }
    const QcMobDefinition &definition = m_state.content().mobs()[mob->type];
    const std::optional<QcAabb> body = m_state.entities().boxOf(*mob);
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    if ((centre - eye).length() > QcServerCombat::attackReach + definition.width + 1.0 || !body
        || !QcLineOfSight::canSee(m_state.world(), client, *body)) {
        return true;
    }

    if (!mob->saddled) {
        return saddle(client, *mob);
    }
    const bool inVehicle = mobs.riddenBy(client.id) || m_state.entities().minecarts().ridden(client.id) || m_state.entities().boats().drivenBy(client.id);
    if (mob->rider == 0 && !inVehicle) {
        mobs.seatRider(*mob, client.id);
        client.mining.pos.reset();
        seat(client, *mob);
    }
    return true;
}

bool QcServerMounts::saddle(QcServerClient &client, QcCreature &mob)
{
    const QcMobDefinition &definition = m_state.content().mobs()[mob.type];
    const QcItemStack &held = client.inventory.selectedStack();
    if (held.isEmpty() || !definition.ride || held.item != definition.ride->saddleId) {
        return false;
    }
    mob.saddled = true;
    // What a player put on it stays with the world.
    m_state.entities().creatures().keep(mob, true);
    if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_inventory.send(client);
    m_sounds.play(QString::fromLatin1(QcSoundKeys::armourEquip), mob.motion.position, 0.6f);
    return true;
}

bool QcServerMounts::steer(QcServerClient &client, const QcPlayerInput &input)
{
    if (!m_state.entities().creatures().riddenBy(client.id)) {
        return false;
    }
    client.yaw = input.yaw;
    client.pitch = input.pitch;
    client.sneaking = false;
    client.sprinting = false;
    if (input.sneak) {
        leave(client);
    }
    return true;
}

void QcServerMounts::leave(QcServerClient &client)
{
    QcMobManager &mobs = m_state.entities().creatures();
    const QcCreature *mob = mobs.riddenBy(client.id);
    if (!mob) {
        return;
    }
    client.motion.position = exitSpot(m_state.world(), *mob);
    mobs.dropRider(client.id);
    client.motion.velocity = {};
    client.motion.onGround = false;
    client.fallPeak = client.motion.position.y;
    client.stateDirty = true;
}

void QcServerMounts::tick()
{
    QcMobManager &mobs = m_state.entities().creatures();
    // A copy: dropping a rider changes the list. Nobody rides most of the time.
    const QHash<quint32, quint32> riders = mobs.riders();
    for (auto it = riders.cbegin(); it != riders.cend(); ++it) {
        const auto riding = std::ranges::find_if(m_state.clients(), [&it](const auto &client) { return client->id == it.key(); });
        QcServerClient *client = riding != m_state.clients().end() ? *riding : nullptr;
        const QcCreature *mob = std::as_const(mobs).riddenBy(it.key());
        if (mob && client && !client->dead) {
            seat(*client, *mob);
            continue;
        }

        // The creature died, was removed or went elsewhere, or the rider left: he is told, and he
        // falls from where he sat.
        mobs.dropRider(it.key());
        if (client) {
            client->stateDirty = true;
        }
    }
}

quint32 QcServerMounts::vehicleOf(const QcServerClient &client) const
{
    const QcMobManager &mobs = std::as_const(m_state).entities().creatures();
    const QcCreature *mob = mobs.riddenBy(client.id);
    return mob ? mob->id : 0;
}

QcVec3 QcServerMounts::exitSpot(const QcBlockAccess &world, const QcCreature &mob)
{
    const int cells = QcSafeSpot::cellsOf(QcPhysics::playerHeight);
    if (const std::optional<QcVec3> ground = QcSafeSpot::standBeside(world, mob.motion.position, exitReach, cells)) {
        return *ground;
    }

    // No shore near: the best there is, in the open above whatever carries the creature.
    const QcVec3 seated = mob.motion.position + QcVec3{0.0, seatOf(world.content().mobs()[mob.type]), 0.0};
    return QcSafeSpot::nearest(world, seated, {}, cells).value_or(seated);
}

void QcServerMounts::seat(QcServerClient &client, const QcCreature &mob) const
{
    client.motion.position = mob.motion.position + QcVec3{0.0, seatOf(m_state.content().mobs()[mob.type]), 0.0};
    client.motion.velocity = mob.motion.velocity;
    client.motion.onGround = true;
    client.motion.inWater = false;
    client.motion.flying = false;
    client.fallPeak = client.motion.position.y;
    client.stateDirty = true;
}
