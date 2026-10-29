// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/core/QcServerState.h"

#include "common/diagnostics/QcRoundTripStats.h"
#include "server/villages/QcVillagePopulation.h"

#include <utility>

QcServerState::QcServerState(QcServerConfig serverConfig, std::shared_ptr<const QcContentRegistry> registry,
                             std::unique_ptr<QcWorldGenerator> generator)
    : m_config(std::move(serverConfig))
    , m_content(registry)
    , m_world(std::move(registry), std::move(generator))
    , m_entities(m_content, static_cast<quint64>(m_config.seed), m_clients)
    , m_spawnPoint(m_world, m_config, m_clients)
    , m_contactDamage(*m_content)
    , m_plants(m_content, static_cast<quint64>(m_config.seed))
    , m_growth(m_content, static_cast<quint64>(m_config.seed))
    , m_machineRules(m_content)
    , m_liquids(m_content)
    , m_random(static_cast<quint32>(static_cast<quint64>(m_config.seed) ^ 0xD2095u))
    , m_soundRandom(static_cast<quint32>(static_cast<quint64>(m_config.seed) ^ 0x50D5u))
{
    // Villagers claim beds and workstations in the village state (decision E-411).
    m_entities.creatures().population().setVillages(&m_world.villages());
    applyLimits();
}

void QcServerState::applyLimits()
{
    const QcServerLimits &limits = m_config.limits;
    m_circuits.maxPendingChanges = static_cast<qsizetype>(limits.signalMaxPendingChanges);
    m_hoppers.cooldownTicks = static_cast<int>(limits.hopperCooldownTicks);
    m_world.blockEntities().setLimits({static_cast<qsizetype>(limits.maxContainers), static_cast<qsizetype>(limits.maxMachines),
        static_cast<qsizetype>(limits.maxSaplings)});
    m_liquids.setMaxPlanned(static_cast<qsizetype>(limits.liquidPlannedSteps));
    m_world.fires().setLimits({static_cast<qsizetype>(limits.firesPerChunk), static_cast<qsizetype>(limits.fires)});
    m_world.villages().setLimits({static_cast<qsizetype>(limits.villages), static_cast<qsizetype>(limits.villageBeds),
        static_cast<qsizetype>(limits.villageWorkstations)});
    m_entities.applyLimits(limits);
    m_growth.setRandomTicksPerSection(static_cast<int>(limits.randomTicksPerSection));
    m_plants.setLeafLimits(limits.leafDecaysPerTick, limits.leafChecksPerTick, limits.leafPendingChecks);
}

QcPermission QcServerState::permissionOf(const QcServerClient &client) const
{
    return client.local || m_access.isOperator(client.name, client.login.fingerprint) ? QcPermission::Operator : QcPermission::Player;
}

bool QcServerState::isLoadedAt(QcBlockPos pos) const
{
    return m_world.chunk(pos.chunk()) != nullptr;
}

const QcItemDefinition *QcServerState::heldItem(const QcServerClient &client) const
{
    const QcItemStack &held = client.inventory.selectedStack();
    return !held.isEmpty() && m_content->isValidItem(held.item) ? &m_content->item(held.item) : nullptr;
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
