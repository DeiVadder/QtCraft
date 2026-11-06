// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/dimensions/QcServerDimension.h"

#include "server/villages/QcVillagePopulation.h"

#include <utility>

QcServerDimension::QcServerDimension(QcDimensionDefinition definition, const std::shared_ptr<const QcContentRegistry> &content,
                                     std::unique_ptr<QcWorldGenerator> generator, quint64 seed)
    : m_definition(std::move(definition))
    , m_world(content, std::move(generator))
    , m_entities(content, seed, m_clients)
    , m_plants(content, seed)
    , m_growth(content, seed)
    , m_liquids(content, m_definition.liquidFlows)
{
    // Villagers claim beds and workstations in the village state (decision E-411).
    m_entities.creatures().population().setVillages(&m_world.villages());
}
