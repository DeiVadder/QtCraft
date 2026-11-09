// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcWorldMirror.h"

#include "common/physics/QcPhysics.h"

#include <algorithm>

std::optional<QcWorldMirror::Change> QcWorldMirror::handle(const QcMessage &message, const QcVec3 &eye, quint64 now)
{
    if (const auto *chunkData = std::get_if<QcMsg::ChunkData>(&message)) {
        m_extras.chunkArrived(chunkData->chunk->pos());
    } else if (const auto *unload = std::get_if<QcMsg::ChunkUnload>(&message)) {
        m_extras.removeChunk(unload->pos);
    }
    if (const auto *dimension = std::get_if<QcMsg::DimensionChange>(&message)) {
        enter(dimension->dimension);
    }
    if (std::optional<Change> change = m_blocks.handle(message)) {
        return change;
    }
    if (handleSkyMessage(message, eye, now)) {
        return Change{};
    }
    return std::nullopt;
}

bool QcWorldMirror::handleSkyMessage(const QcMessage &message, const QcVec3 &eye, quint64 now)
{
    if (const auto *labels = std::get_if<QcMsg::ChunkLabels>(&message)) {
        m_extras.setLabels(*labels);
    } else if (const auto *spawner = std::get_if<QcMsg::SpawnerDisplay>(&message)) {
        m_extras.setSpawner(*spawner);
    } else if (const auto *sign = std::get_if<QcMsg::SignText>(&message)) {
        m_extras.setSign(*sign);
    } else if (const auto *campfire = std::get_if<QcMsg::CampfireDisplay>(&message)) {
        m_extras.setCampfire(*campfire);
    } else if (const auto *map = std::get_if<QcMsg::MapUpdate>(&message)) {
        m_maps.apply(*map);
    } else if (const auto *explosion = std::get_if<QcMsg::Explosion>(&message)) {
        m_extras.addExplosion(*explosion, now);
    } else if (const auto *time = std::get_if<QcMsg::WorldTime>(&message)) {
        m_worldTime = *time;
    } else if (const auto *weather = std::get_if<QcMsg::Weather>(&message)) {
        m_weather.receive(*weather);
    } else if (const auto *strike = std::get_if<QcMsg::Lightning>(&message)) {
        m_weather.lightning(*strike, eye, now);
    } else if (const auto *atmosphere = std::get_if<QcMsg::Atmosphere>(&message)) {
        m_atmosphere.receive(*atmosphere);
    } else {
        return false;
    }
    return true;
}

void QcWorldMirror::enter(const QcDimensionDefinition &dimension)
{
    // What the chunks carried, the clouds of ink, the weather and the atmosphere belong to the old
    // dimension; those of the new one come from the server (decisions E-1002, E-1120), so no fog of
    // the old one blends into the new. Maps are items of the world.
    m_dimension = dimension;
    m_extras = {};
    m_ink = {};
    m_weather = {};
    m_atmosphere = {};
}

double QcWorldMirror::timeAt(double alpha) const
{
    if (!m_worldTime.running) {
        return m_worldTime.time;
    }
    return m_worldTime.time + std::clamp(alpha, 0.0, 1.0) * QcPhysics::tickDuration * m_worldTime.speed;
}

void QcWorldMirror::tick(quint64 now)
{
    m_extras.pruneExplosions(now);
    m_ink.prune(now);
    m_effects.tick(m_worldTime.speed);
    m_atmosphere.tick(QcPhysics::tickDuration);
    if (m_worldTime.running) {
        // At another tick rate of the server (or frozen) the day runs faster or slower (decision E-249).
        m_worldTime.time += QcPhysics::tickDuration * m_worldTime.speed;
    }
}
