// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/machines/QcServerBeacons.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcBeacon.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerEffects.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/machines/QcBlockScreens.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// The sounds of a beacon (the reference's block.beacon.*).
constexpr QLatin1StringView activateSound("block.beacon.activate");
constexpr QLatin1StringView deactivateSound("block.beacon.deactivate");
constexpr QLatin1StringView ambientSound("block.beacon.ambient");
constexpr QLatin1StringView powerSelectSound("block.beacon.power_select");

// The middle of the beacon's cell, where its sounds come from.
QcVec3 middleOf(QcBlockPos pos)
{
    return {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
}

} // namespace

QcServerBeacons::QcServerBeacons(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
    , m_screens(screens)
{
    // One screen at a time: a block's, cart's or villager's screen opening closes the beacon.
    m_screens.setWorkstationCloser(QcWorkstation::Beacon, [this](QcServerClient &client) { close(client, true); });
}

bool QcServerBeacons::open(QcServerClient &client, QcBlockPos pos)
{
    // The beacons' table follows the blocks: an entry stands for a beacon block.
    if (!m_state.world().beacons().at(pos)) {
        return false;
    }

    const bool same = client.screen.beacon && client.screen.beacon->pos == pos;
    m_screens.close(client, true);
    m_screens.endTrade(client);
    m_screens.closeWorkstationsExcept(client, QcWorkstation::Beacon);
    if (!same) {
        m_inventory.returnScreenInputs(client);
        client.screen.beacon = QcServerClient::Beacon{pos, {}};
    }
    client.connection->send(QcMsg::OpenBeacon{pos});
    m_inventory.send(client);
    return true;
}

void QcServerBeacons::close(QcServerClient &client, bool notify)
{
    if (!client.screen.beacon) {
        return;
    }
    m_inventory.returnScreenInputs(client);
    client.screen.beacon.reset();
    if (client.state == QcServerClient::State::Closed) {
        return;
    }
    if (notify) {
        client.connection->send(QcMsg::CloseBeacon{});
    }
    m_inventory.send(client);
}

bool QcServerBeacons::canStayOpen(const QcServerClient &client) const
{
    return client.screen.beacon && !client.dead && client.rules().interacts && m_state.world().beacons().at(client.screen.beacon->pos)
           && QcLineOfSight::isWithinReach(client, client.screen.beacon->pos);
}

QcMsg::BeaconState QcServerBeacons::stateOf(const QcServerState &state, const QcServerClient &client)
{
    QcMsg::BeaconState message;
    if (!client.screen.beacon) {
        return message;
    }
    message.payment = client.screen.beacon->payment;
    if (const QcBeacons::Entry *entry = state.world().beacons().at(client.screen.beacon->pos)) {
        message.level = entry->isActive() ? entry->level : 0;
        message.primary = entry->primary;
        message.secondary = entry->secondary;
    }
    return message;
}

QcMsg::BeaconBeam QcServerBeacons::beamOf(const QcBeacons &beacons, QcBlockPos pos)
{
    const QcBeacons::Entry *entry = beacons.at(pos);
    return {pos, entry && entry->isActive() ? entry->beam : QList<QcBeamSection>()};
}

bool QcServerBeacons::accepts(const QcServerClient &client, const QcBeacons::Entry &entry, const QcItemStack &payment,
                              const QcMsg::ChooseBeaconEffects &choice) const
{
    // The screen offers only what the beacon may give; anything else came from a stale or
    // altered client (decision E-688).
    const int level = entry.isActive() ? entry.level : 0;
    if (payment.isEmpty() || !QcBeacon::isPayment(m_state.content(), payment.item)) {
        qCWarning(lcServer) << "Beacon: choice of" << client.name << "refused, no payment in the slot";
        return false;
    }
    if (!QcBeacon::mayChoose(level, choice.primary, choice.secondary)) {
        qCWarning(lcServer) << "Beacon: choice of" << client.name << "refused:" << choice.primary << choice.secondary << "not allowed at level"
                            << level;
        return false;
    }
    if (!m_state.content().effect(choice.primary) || (!choice.secondary.isEmpty() && !m_state.content().effect(choice.secondary))) {
        qCWarning(lcServer) << "Beacon: choice of" << client.name << "refused:" << choice.primary << choice.secondary << "unknown effect";
        return false;
    }
    return true;
}

void QcServerBeacons::choose(QcServerClient &client, const QcMsg::ChooseBeaconEffects &choice)
{
    if (!client.screen.beacon || !canStayOpen(client)) {
        qCDebug(lcServer) << "Beacon: choice by" << client.name << "refused, no beacon open or in reach";
        close(client, true);
        return;
    }

    QcItemStack &payment = client.screen.beacon->payment;
    QcBeacons::Entry *entry = m_state.world().beacons().at(client.screen.beacon->pos);
    if (!entry || !accepts(client, *entry, payment, choice)) {
        m_inventory.send(client);
        return;
    }

    entry->primary = choice.primary;
    entry->secondary = choice.secondary;
    payment = payment.count > 1 ? payment.withCount(payment.count - 1) : QcItemStack();
    m_sounds.play(QString(powerSelectSound), middleOf(entry->pos));
    m_inventory.send(client);
}

void QcServerBeacons::update()
{
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing && client->screen.beacon && !canStayOpen(*client)) {
            close(*client, true);
        }
    }
    // A beacon that is gone takes its beam with it at once.
    const QList<QcBlockPos> darkened = m_state.world().beacons().takeDarkened();
    for (const QcBlockPos &pos : darkened) {
        announce(QcMsg::BeaconBeam{pos, {}});
    }
    if (m_state.clock().tick() % QcBeacon::checkIntervalTicks == 0) {
        lookAtAll();
    }
}

void QcServerBeacons::lookAtAll()
{
    const QcServerWorld &world = m_state.world();
    const QcBeacon::BlockAt blockAt = [&world](QcBlockPos pos) { return world.blockAt(pos); };
    const QList<QcBlockPos> positions = m_state.world().beacons().positions();
    for (const QcBlockPos &pos : positions) {
        if (QcBeacons::Entry *entry = m_state.world().beacons().at(pos)) {
            react(*entry, entry->look(m_state.content(), blockAt));
        }
    }
}

void QcServerBeacons::react(const QcBeacons::Entry &entry, QcBeacons::Entry::Change change)
{
    const bool wasActive = change.wasActive;
    const int levelBefore = change.levelBefore;
    if (entry.isActive()) {
        giveEffects(entry);
        m_sounds.play(QString(ambientSound), middleOf(entry.pos));
        if (!wasActive || entry.level > levelBefore) {
            noteConstructed(entry);
        }
    }
    if (entry.isActive() != wasActive) {
        m_sounds.play(QString(entry.isActive() ? activateSound : deactivateSound), middleOf(entry.pos));
    }
    if (change.beamChanged || entry.isActive() != wasActive) {
        announce(beamOf(m_state.world().beacons(), entry.pos));
    }
    if (entry.isActive() != wasActive || entry.level != levelBefore) {
        refreshScreens(entry.pos);
    }
}

void QcServerBeacons::refreshScreens(QcBlockPos pos)
{
    // An open screen shows the level the beacon has now.
    for (const auto &client : m_state.clients()) {
        const std::optional<QcServerClient::Beacon> &screen = client->screen.beacon;
        if (client->state == QcServerClient::State::Playing && screen.has_value() && screen->pos == pos) {
            m_inventory.send(*client);
        }
    }
}

void QcServerBeacons::giveEffects(const QcBeacons::Entry &entry)
{
    const QList<QcStatusEffect> effects = QcBeacon::effectsOf(entry.level, entry.primary, entry.secondary);
    if (effects.isEmpty()) {
        return;
    }
    for (const auto &client : m_state.clients()) {
        const QcAabb body = QcPhysics::boxAt(client->motion.position, QcPhysics::playerWidth, QcPhysics::playerHeight);
        if (client->state != QcServerClient::State::Playing || client->dead || !QcBeacon::reaches(entry.pos, entry.level, body)) {
            continue;
        }
        for (const QcStatusEffect &effect : effects) {
            QcServerEffects::givePlayer(m_state, *client, effect);
        }
    }
}

void QcServerBeacons::noteConstructed(const QcBeacons::Entry &entry)
{
    // The reference's box: 10 blocks across, from 9 below the beacon to 5 above it.
    constexpr double across = 10.0;
    constexpr double below = 9.0;
    constexpr double above = 5.0;
    for (const auto &client : m_state.clients()) {
        const QcVec3 &feet = client->motion.position;
        const bool near = std::abs(feet.x - entry.pos.x) <= across && std::abs(feet.z - entry.pos.z) <= across && feet.y >= entry.pos.y - below
                          && feet.y <= entry.pos.y + above;
        if (client->state == QcServerClient::State::Playing && near) {
            m_state.advancements().trigger(*client, {.trigger = QcAdvancementTrigger::BeaconConstructed, .level = entry.level});
        }
    }
}

void QcServerBeacons::announce(const QcMsg::BeaconBeam &beam)
{
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing && client->view.sentChunks.contains(beam.pos.chunk())) {
            client->connection->send(beam);
        }
    }
}
