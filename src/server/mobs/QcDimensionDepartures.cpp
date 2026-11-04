// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/mobs/QcDimensionDepartures.h"

#include "server/core/QcServerState.h"

QcDimensionDepartures::QcDimensionDepartures(QcServerState &state, QcMobManager::DepartureListener listener)
    : m_state(state)
    , m_listener(std::move(listener))
{
    m_state.addDimensionWiring([this](QcServerDimension &dimension) {
        m_tokens.insert(&dimension, dimension.entities().creatures().onDeparture([this, &dimension](const QcCreature &mob, QcMobManager::Departure why) {
            const QcServerState::Scope scope(m_state, dimension);
            m_listener(mob, why);
        }));
    });
}

QcDimensionDepartures::~QcDimensionDepartures()
{
    for (const std::unique_ptr<QcServerDimension> &dimension : m_state.dimensions().all()) {
        if (const auto token = m_tokens.constFind(dimension.get()); token != m_tokens.cend()) {
            dimension->entities().creatures().removeDepartureListener(*token);
        }
    }
}
