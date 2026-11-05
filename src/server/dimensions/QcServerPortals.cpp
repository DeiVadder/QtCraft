// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/dimensions/QcServerPortals.h"

#include "common/physics/QcPhysics.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/dimensions/QcEntityTravel.h"
#include "server/dimensions/QcPlayerTravel.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerInventory.h"

#include <cmath>

namespace {

int ticksOf(double seconds)
{
    return static_cast<int>(std::ceil(seconds / QcPhysics::tickDuration));
}

// A number from the position of a portal, so that portals do not hum in step.
quint64 phaseOf(QcBlockPos pos)
{
    return static_cast<quint64>(static_cast<quint32>(pos.x * 73 + pos.y * 19 + pos.z * 41));
}

QcVec3 middleOf(QcBlockPos cell)
{
    return {cell.x + 0.5, cell.y + 0.5, cell.z + 0.5};
}

} // namespace

QcServerPortals::QcServerPortals(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_blocks(blocks)
    , m_sounds(sounds)
    , m_inventory(inventory)
    , m_links(state, blocks)
{
}

bool QcServerPortals::lightByFire(QcBlockPos pos)
{
    const QString &here = m_state.current().name();
    for (const QcPortalDefinition &portal : m_state.content().worldgen().portals()) {
        if (portal.trigger != QcPortalDefinition::Trigger::Fire || !portal.lightsIn(here)) {
            continue;
        }
        const std::optional<QcPortalFrame> frame = QcPortalShapes::find(m_state.world(), portal, pos, QcPortalShapes::Inside::Empty);
        if (frame && m_links.light(portal, *frame)) {
            if (!portal.litSound.isEmpty()) {
                m_sounds.play(portal.litSound, frame->centre());
            }
            return true;
        }
    }
    return false;
}

bool QcServerPortals::use(QcServerClient &client, const QcMsg::UseItem &message)
{
    const QcItemDefinition *held = m_state.heldItem(client);
    const std::optional<QcBlockId> clicked = m_state.world().blockAt(message.target);
    if (held == nullptr || !clicked || client.dead) {
        return false;
    }

    const QcContentRegistry &content = m_state.content();
    for (const QcPortalDefinition &portal : content.worldgen().portals()) {
        const bool empty = QcPortalShapes::isFrame(content, portal, *clicked, false) && !QcPortalShapes::isFrame(content, portal, *clicked, true);
        if (portal.trigger != QcPortalDefinition::Trigger::Item || portal.triggerItem != held->name || !empty) {
            continue;
        }
        if (!portal.lightsIn(m_state.current().name()) || !QcLineOfSight::isWithinReach(client, message.target)) {
            return true;
        }
        m_blocks.setAndBroadcast(message.target, content.withProperty(*clicked, portal.insertProperty, portal.insertValue));
        if (!client.rules().infiniteItems) {
            client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
        }
        m_inventory.send(client);
        if (const std::optional<QcPortalFrame> ring = QcPortalShapes::ringBeside(m_state.world(), portal, message.target)) {
            m_links.light(portal, *ring);
            if (!portal.litSound.isEmpty()) {
                m_sounds.play(portal.litSound, ring->centre());
            }
        }
        return true;
    }
    return false;
}

void QcServerPortals::tick()
{
    notePlayers();
    noteEntities();
    hum();
    forgetGone();
}

const QcPortalDefinition *QcServerPortals::portalAt(QcBlockPos cell) const
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(cell);
    return block ? QcPortalShapes::portalOfBlock(m_state.content(), *block) : nullptr;
}

void QcServerPortals::notePlayers()
{
    for (const QcServerClient *client : m_state.clients()) {
        if (!client->hasBody()) {
            m_travellers.stepOut(client->id);
            continue;
        }
        const QcBlockPos feet = QcPhysics::blockContaining(client->motion.position);
        const QcPortalDefinition *portal = portalAt(feet);
        const QcBlockPos cell = portal != nullptr ? feet : feet.offset(0, 1, 0);
        portal = portal != nullptr ? portal : portalAt(cell);
        if (portal == nullptr) {
            m_travellers.stepOut(client->id);
            continue;
        }
        const double wait = client->gameMode == QcGameMode::Creative ? portal->creativeWait : portal->survivalWait;
        if (m_travellers.stand(client->id, ticksOf(wait))) {
            m_due.append({QcEntityKind::Player, client->id, &m_state.current(), portal, cell});
        }
    }
}

void QcServerPortals::noteEntities()
{
    const QList<QcPortalIndex::Entry> &entries = m_state.current().portals().entries();
    for (const QcPortalIndex::Entry &entry : entries) {
        if (!m_state.isLoadedAt(entry.frame.origin)) {
            continue;
        }
        for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
            noteEntity(QcEntityKind::Creature, mob.id, mob.motion.position, entry);
        }
        for (const QcItemEntity &item : m_state.entities().items().items()) {
            noteEntity(QcEntityKind::Item, item.id, item.motion.position, entry);
        }
        for (const QcMinecart &cart : m_state.entities().minecarts().all()) {
            // A cart with a rider waits for him: he travels by his own wait, out of the cart.
            if (cart.rider == 0) {
                noteEntity(QcEntityKind::Minecart, cart.id, cart.motion.position, entry);
            }
        }
    }
}

void QcServerPortals::noteEntity(QcEntityKind kind, quint32 id, const QcVec3 &position, const QcPortalIndex::Entry &entry)
{
    const QcBlockPos cell = QcPhysics::blockContaining(position + QcVec3{0.0, 0.1, 0.0});
    if (!entry.frame.contains(cell) || !m_travellers.mayTravel(id, m_state.clock().tick())) {
        return;
    }
    const QcPortalDefinition *portal = portalAt(cell);
    if (portal != nullptr && portal->name == entry.portal) {
        m_due.append({kind, id, &m_state.current(), portal, cell});
    }
}

void QcServerPortals::hum()
{
    const quint64 tick = m_state.clock().tick();
    for (const QcPortalIndex::Entry &entry : m_state.current().portals().entries()) {
        const QcPortalDefinition *portal = m_state.content().worldgen().portal(entry.portal);
        if (portal == nullptr || portal->ambientSound.isEmpty() || (tick + phaseOf(entry.frame.origin)) % humEveryTicks != 0) {
            continue;
        }
        const QcVec3 centre = entry.frame.centre();
        const bool heard = std::ranges::any_of(m_state.clients(), [&centre](const QcServerClient *client) {
            return (client->motion.position - centre).length() <= humRange;
        });
        if (heard && m_state.isLoadedAt(entry.frame.origin)) {
            m_sounds.play(portal->ambientSound, centre);
        }
    }
}

void QcServerPortals::forgetGone()
{
    if (m_state.clock().tick() % forgetEveryTicks != 0) {
        return;
    }
    m_travellers.forgetRested(m_state.clock().tick());
    QSet<quint32> playing;
    for (const auto &client : m_state.allClients()) {
        playing.insert(client->id);
    }
    m_travellers.keepPlayers(playing);
    m_state.current().portals().forgetGone([this](const QcPortalIndex::Entry &entry) {
        const std::optional<QcBlockId> block = m_state.world().blockAt(entry.frame.origin);
        const QcPortalDefinition *portal = block ? QcPortalShapes::portalOfBlock(m_state.content(), *block) : nullptr;
        return block && (portal == nullptr || portal->name != entry.portal);
    });
}

void QcServerPortals::travel()
{
    const QList<Departure> due = std::exchange(m_due, {});
    for (const Departure &departure : due) {
        carry(departure);
    }
}

void QcServerPortals::carry(const Departure &departure)
{
    const QcServerState::Scope scope(m_state, *departure.from);
    QcServerClient *player = departure.kind == QcEntityKind::Player ? playerOf(departure) : nullptr;
    if (departure.kind == QcEntityKind::Player && player == nullptr) {
        return;
    }
    // The portal it leaves by is known from now on: the way back finds it (decision E-1023).
    if (const std::optional<QcPortalFrame> frame = QcPortalShapes::find(m_state.world(), *departure.portal, departure.cell, QcPortalShapes::Inside::Lit)) {
        m_state.current().portals().add({departure.portal->name, *frame});
    }
    const std::optional<QcPortalLinks::Destination> destination = m_links.destination(*departure.portal, middleOf(departure.cell));
    if (!destination) {
        return;
    }

    if (player != nullptr) {
        QcPlayerTravel::move(m_state, *player, *destination->dimension, destination->position);
        m_travellers.playerArrived(departure.id);
    } else if (QcEntityTravel::move({departure.kind, departure.id, departure.from, destination->dimension, destination->position})) {
        const quint64 rested = m_state.clock().tick() + static_cast<quint64>(ticksOf(departure.portal->cooldown));
        m_travellers.entityArrived(departure.id, rested);
    } else {
        return;
    }
    if (!departure.portal->travelSound.isEmpty()) {
        const QcServerState::Scope there(m_state, *destination->dimension);
        m_sounds.play(departure.portal->travelSound, destination->position);
    }
}

QcServerClient *QcServerPortals::playerOf(const Departure &departure)
{
    const auto &clients = m_state.allClients();
    const auto it = std::ranges::find_if(clients, [&departure](const auto &client) { return client->id == departure.id; });
    if (it == clients.end() || !(*it)->hasBody() || &m_state.dimensionOf(**it) != departure.from) {
        return nullptr;
    }
    return it->get();
}
