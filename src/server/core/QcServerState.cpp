// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/core/QcServerState.h"

#include "common/diagnostics/QcRoundTripStats.h"
#include "common/protocol/QcConnection.h"
#include "server/villages/QcVillagePopulation.h"

#include <utility>

namespace {

// The limits of what a dimension keeps itself: block entities, fires, villages, circuits, entities.
void applyDimensionLimits(QcServerDimension &dimension, const QcServerLimits &limits)
{
    QcServerWorld &world = dimension.world();
    world.blockEntities().setLimits({static_cast<qsizetype>(limits.maxContainers), static_cast<qsizetype>(limits.maxMachines),
        static_cast<qsizetype>(limits.maxSaplings)});
    world.fires().setLimits({static_cast<qsizetype>(limits.firesPerChunk), static_cast<qsizetype>(limits.fires)});
    world.villages().setLimits({static_cast<qsizetype>(limits.villages), static_cast<qsizetype>(limits.villageBeds),
        static_cast<qsizetype>(limits.villageWorkstations)});
    dimension.circuits().maxPendingChanges = static_cast<qsizetype>(limits.signalMaxPendingChanges);
    dimension.hoppers().cooldownTicks = static_cast<int>(limits.hopperCooldownTicks);
    dimension.liquids().setMaxPlanned(static_cast<qsizetype>(limits.liquidPlannedSteps));
    dimension.entities().applyLimits(limits);
    dimension.growth().setRandomTicksPerSection(static_cast<int>(limits.randomTicksPerSection));
    dimension.plants().setLeafLimits(limits.leafDecaysPerTick, limits.leafChecksPerTick, limits.leafPendingChecks);
}

} // namespace

QcServerState::QcServerState(QcServerConfig serverConfig, std::shared_ptr<const QcContentRegistry> registry,
                             std::unique_ptr<QcWorldGenerator> generator)
    : m_config(std::move(serverConfig))
    , m_content(registry)
    , m_dimensions(std::move(registry), std::move(generator), static_cast<quint64>(m_config.seed))
    , m_spawnPoint(world(), m_config, m_dimensions.overworld().clients())
    , m_contactDamage(*m_content)
    , m_machineRules(m_content)
    , m_random(static_cast<quint32>(static_cast<quint64>(m_config.seed) ^ 0xD2095u))
    , m_soundRandom(static_cast<quint32>(static_cast<quint64>(m_config.seed) ^ 0x50D5u))
{
    applyLimits();
}

void QcServerState::applyLimits()
{
    for (const std::unique_ptr<QcServerDimension> &dimension : dimensions().all()) {
        applyDimensionLimits(*dimension, m_config.limits);
    }
}

QcResult<QcServerDimension *> QcServerState::openDimension(const QString &name)
{
    if (QcServerDimension *open = dimensions().find(name)) {
        return open;
    }

    QcResult<QcServerDimension *> opened = dimensions().open(name, config().seed, info());
    if (!opened) {
        return opened;
    }
    QcServerDimension &dimension = **opened;
    applyDimensionLimits(dimension, config().limits);
    for (const DimensionWiring &wiring : m_dimensionWirings) {
        wiring(dimension);
    }
    if (m_dimensionStorage) {
        const Scope scope(*this, dimension);
        if (const QcStatus stored = m_dimensionStorage(dimension); !stored) {
            dimensions().dropLast();
            return qcError(stored.error());
        }
    }
    if (QcDimensions::isVersioned(info())) {
        info().dimensionGeneratorVersions.insert(name, dimension.generatorVersion());
    }
    return opened;
}

void QcServerState::addDimensionWiring(DimensionWiring wiring)
{
    for (const std::unique_ptr<QcServerDimension> &dimension : dimensions().all()) {
        wiring(*dimension);
    }
    m_dimensionWirings.push_back(std::move(wiring));
}

void QcServerState::closeScreens(QcServerClient &client) const
{
    if (m_screenCloser) {
        m_screenCloser(client);
    }
}

void QcServerState::addClient(std::unique_ptr<QcServerClient> client)
{
    dimensions().overworld().addClient(client.get());
    m_clients.push_back(std::move(client));
}

qsizetype QcServerState::removeClosedClients()
{
    for (const std::unique_ptr<QcServerClient> &client : m_clients) {
        if (client->state == QcServerClient::State::Closed) {
            dimensionOf(*client).removeClient(client.get());
        }
    }
    return std::erase_if(m_clients, [](const std::unique_ptr<QcServerClient> &client) { return client->state == QcServerClient::State::Closed; });
}

QcServerDimension &QcServerState::dimensionOf(const QcServerClient &client)
{
    QcServerDimension *dimension = dimensions().find(client.dimension);
    return dimension != nullptr ? *dimension : dimensions().overworld();
}

void QcServerState::moveClient(QcServerClient &client, QcServerDimension &dimension)
{
    dimensionOf(client).removeClient(&client);
    dimension.addClient(&client);
    client.dimension = dimension.name();

    // Nothing he was sent of the old dimension stands in the new one; the entities he knew go with
    // the next sync, as they are not there.
    for (const QcChunkPos &chunk : std::as_const(client.view.sentChunks)) {
        client.connection->send(QcMsg::ChunkUnload{chunk});
    }
    client.view.sentChunks.clear();
    ++client.view.sentChanges;
}

QcPermission QcServerState::permissionOf(const QcServerClient &client) const
{
    return client.local || m_access.isOperator(client.name, client.login.fingerprint) ? QcPermission::Operator : QcPermission::Player;
}

bool QcServerState::isLoadedAt(QcBlockPos pos) const
{
    return world().chunk(pos.chunk()) != nullptr;
}

const QcItemDefinition *QcServerState::heldItem(const QcServerClient &client) const
{
    const QcItemStack &held = client.inventory.selectedStack();
    return !held.isEmpty() && content().isValidItem(held.item) ? &content().item(held.item) : nullptr;
}

void QcServerState::reportLatency(QcServerClient &client, qint32 reportedUs)
{
    if (QcListedLatency::changed(client.listedLatencyUs, reportedUs)) {
        client.listedLatencyUs = reportedUs;
        m_latencyChanged = true;
    }
}

bool QcServerState::takePlayerListChanged()
{
    const qint64 now = m_clock.playerTimeNs();
    const bool latencyDue = m_latencyChanged && now - m_playerListNs >= latencyListIntervalNs;
    if (!std::exchange(m_playerListChanged, false) && !latencyDue) {
        return false;
    }

    m_latencyChanged = false;
    m_playerListNs = now;
    return true;
}
