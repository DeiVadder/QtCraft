// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcInkClouds.h"

#include "clientcore/session/QcClientEntity.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcEntityMessages.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <numbers>

void QcInkClouds::add(const QcVec3 &centre, const QString &texture, quint32 id, quint64 tick)
{
    if (m_puffs.size() + puffsPerCloud > maxPuffs) {
        return;
    }

    // The same creature hit in the same tick gives the same cloud.
    QRandomGenerator random(static_cast<quint32>((static_cast<quint64>(id) * 2654435761ull) ^ tick));
    for (int index = 0; index < puffsPerCloud; ++index) {
        const double angle = 2.0 * std::numbers::pi * (index + random.bounded(0.6)) / puffsPerCloud;
        const double rise = random.bounded(1.2) - 0.6;
        const double speed = 0.7 + random.bounded(0.9);
        Puff puff;
        puff.centre = centre;
        puff.velocity = QcVec3{std::cos(angle), rise, std::sin(angle)} * speed;
        puff.startTick = tick;
        puff.lifeTicks = minLifeTicks + random.bounded(maxLifeTicks - minLifeTicks);
        puff.size = 0.1 + random.bounded(0.12);
        puff.texture = texture;
        m_puffs.append(puff);
    }
}

void QcInkClouds::addStruck(const QcMsg::EntityMoves &moves, const QHash<quint32, QcClientEntity> &entities, const QcContentRegistry &content, quint64 tick)
{
    for (const QcMsg::EntityMove &move : moves.moves) {
        if (!move.struck) {
            continue;
        }
        // Where the creature is drawn now (the moves wait in the interpolation buffer), not where
        // the newest move puts it.
        const auto entity = entities.constFind(move.id);
        const QcMobDefinition *kind = entity != entities.constEnd() ? entity->creature(content) : nullptr;
        if (kind && !kind->ink.isEmpty()) {
            add(entity->position + QcVec3{0.0, kind->height * 0.5, 0.0}, kind->ink, move.id, tick);
        }
    }
}

void QcInkClouds::prune(quint64 tick)
{
    m_puffs.removeIf([tick](const Puff &puff) { return tick >= puff.startTick + static_cast<quint64>(puff.lifeTicks); });
}

std::optional<QcInkClouds::Look> QcInkClouds::lookAt(const Puff &puff, double ticks)
{
    const double age = ticks - static_cast<double>(puff.startTick);
    if (age < 0.0 || age >= puff.lifeTicks) {
        return std::nullopt;
    }

    // The water stops the puffs soon; each swells to twice its size and shrinks away at the end.
    const double seconds = age * QcPhysics::tickDuration;
    const double travelled = (1.0 - std::exp(-seconds * 2.5)) / 2.5;
    const double share = age / puff.lifeTicks;
    const double swell = share < 0.3 ? 1.0 + share / 0.3 : 2.0 * (1.0 - share) / 0.7;
    return Look{puff.centre + puff.velocity * travelled, puff.size * std::max(swell, 0.1)};
}
