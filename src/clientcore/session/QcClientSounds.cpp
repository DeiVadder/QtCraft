// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientSounds.h"

#include "common/content/registry/QcSoundKeys.h"
#include "common/physics/QcPhysics.h"

std::optional<QcSoundCue> QcClientSounds::record(QcSoundCue cue, quint64 tick)
{
    if (cue.key.isEmpty()) {
        return std::nullopt;
    }

    cue.tick = tick;
    if (m_recent.size() >= recentLimit) {
        m_recent.removeFirst();
    }
    m_recent.append(cue);
    return cue;
}

void QcClientSounds::updateAmbience(const QcBlockAccess &world, const QcVec3 &eye, double rain, quint64 tick,
                                    const std::function<void(const QcSoundCue &)> &play)
{
    if (tick % ambienceTicks == 1) {
        m_ambienceTarget = QcAmbience::targets(world, eye, rain);

        // A fixed pseudo-random sequence over the samples keeps sessions reproducible.
        const quint64 sample = tick / ambienceTicks;
        const double roll = static_cast<double>((sample * 2654435761ULL) % 1000ULL) / 1000.0;
        if (roll < QcAmbience::lavaPopChance) {
            if (const std::optional<QcBlockPos> lava = QcAmbience::nearestGlowingLiquid(world, eye)) {
                play({QString::fromLatin1(QcSoundKeys::lavaPop), {lava->x + 0.5, lava->y + 1.0, lava->z + 0.5}, 1.0f, 1.0f,
                      QcSoundCue::Origin::Local, 0});
            }
        }
    }

    m_ambience = QcAmbience::approach(m_ambience, m_ambienceTarget, QcPhysics::tickDuration);
}
