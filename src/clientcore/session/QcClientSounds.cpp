// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientSounds.h"

#include "common/content/registry/QcSoundKeys.h"
#include "common/physics/QcPhysics.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

// splitmix64: a well-mixed 64-bit word from a counter.
quint64 splitMix64(quint64 value)
{
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

// The top 53 bits of `word` as a share in [0, 1).
double unitOf(quint64 word)
{
    return static_cast<double>(word >> 11) / static_cast<double>(1ULL << 53);
}

} // namespace

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

void QcClientSounds::updateBiomeAmbience(const QcBiomeAtmosphere &atmosphere, const QcVec3 &eye, quint64 tick,
                                         const std::function<void(const QcSoundCue &)> &play)
{
    // Another loop waits until the old one has faded out.
    const bool same = biomeLoop() == atmosphere.loop;
    const float target = same && !biomeLoop().isEmpty() ? 1.0f : 0.0f;
    const auto step = static_cast<float>(QcAmbience::fadePerSecond * QcPhysics::tickDuration);
    setBiomeLevel(std::clamp(target, biomeLevel() - step, biomeLevel() + step));
    if (!same && biomeLevel() <= QcAmbience::arriveLevel) {
        setBiomeLoop(atmosphere.loop);
        setBiomeLevel(0.0f);
    }

    if (atmosphere.additions.isEmpty() || atmosphere.additionsChance <= 0.0) {
        return;
    }
    // Whether and where from two mixed words of the tick, so sessions stay reproducible.
    if (unitOf(splitMix64(2 * tick)) < atmosphere.additionsChance) {
        const double angle = 2.0 * std::numbers::pi * unitOf(splitMix64(2 * tick + 1));
        const QcVec3 at{eye.x + additionsDistance * std::cos(angle), eye.y, eye.z + additionsDistance * std::sin(angle)};
        play({atmosphere.additions, at, 1.0f, 1.0f, QcSoundCue::Origin::Local, 0});
    }
}
