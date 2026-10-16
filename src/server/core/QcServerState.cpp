#include "QcServerState.h"

#include <utility>

QcServerState::QcServerState(QcServerConfig serverConfig, std::shared_ptr<const QcContentRegistry> registry,
                             std::unique_ptr<QcWorldGenerator> generator)
    : m_config(std::move(serverConfig))
    , m_content(registry)
    , m_world(std::move(registry), std::move(generator))
    , m_entities(m_content, quint64(m_config.seed), m_clients)
    , m_spawnPoint(m_world, m_config, m_clients)
    , m_contactDamage(*m_content)
    , m_plants(m_content, quint64(m_config.seed))
    , m_growth(m_content, quint64(m_config.seed))
    , m_machineRules(m_content)
    , m_liquids(m_content)
    , m_random(quint32(quint64(m_config.seed) ^ 0xD2095u))
    , m_soundRandom(quint32(quint64(m_config.seed) ^ 0x50D5u))
{
    // Villagers claim beds and workstations in the village state (decision E-411).
    m_entities.creatures().setVillages(&m_world.villages());
    applyLimits();
}

void QcServerState::applyLimits()
{
    const QcServerLimits &limits = m_config.limits;
    m_circuits.maxPendingChanges = qsizetype(limits.signalMaxPendingChanges);
    m_hoppers.cooldownTicks = int(limits.hopperCooldownTicks);
    m_world.blockEntities().setLimits({qsizetype(limits.maxContainers), qsizetype(limits.maxMachines), qsizetype(limits.maxSaplings)});
    m_liquids.setMaxPlanned(qsizetype(limits.liquidPlannedSteps));
    m_world.fires().setLimits({qsizetype(limits.firesPerChunk), qsizetype(limits.fires)});
    m_world.villages().setLimits({qsizetype(limits.villages), qsizetype(limits.villageBeds), qsizetype(limits.villageWorkstations)});
    m_entities.applyLimits(limits);
    m_growth.setRandomTicksPerSection(int(limits.randomTicksPerSection));
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
