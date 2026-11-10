// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcCampfireSmoke.h"

#include <cmath>
#include <numbers>

double QcCampfireSmoke::unitOf(QcBlockPos pos, int index, qint64 round, quint32 salt)
{
    quint64 h = static_cast<quint64>(static_cast<qint64>(pos.x)) * 0x9E3779B97F4A7C15ull
        ^ static_cast<quint64>(static_cast<qint64>(pos.y)) * 0xC2B2AE3D27D4EB4Full
                ^ static_cast<quint64>(static_cast<qint64>(pos.z)) * 0x165667B19E3779F9ull;
    h += static_cast<quint64>(index) * 0x27D4EB2F165667C5ull + static_cast<quint64>(round) * 0x94D049BB133111EBull + salt;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
    h ^= h >> 31;
    return static_cast<double>(h >> 11) / static_cast<double>(1ull << 53);
}

QList<QcEffectPuff> QcCampfireSmoke::puffsOf(QcBlockPos pos, double seconds)
{
    QList<QcEffectPuff> puffs;
    for (int index = 0; index < puffsPerCampfire; ++index) {
        // Each puff starts its loop at its own time; every loop drifts its own way.
        const double time = seconds + index * puffSeconds / puffsPerCampfire;
        const auto round = static_cast<qint64>(std::floor(time / puffSeconds));
        const double age = time / puffSeconds - static_cast<double>(round);
        const double driftX = (unitOf(pos, index, round, 1) - 0.5) * 0.8;
        const double driftZ = (unitOf(pos, index, round, 2) - 0.5) * 0.8;
        const double rise = 0.45 + age * 3.2;
        puffs.append({{pos.x + 0.5 + driftX * age, pos.y + rise, pos.z + 0.5 + driftZ * age}, 0.18 + 0.22 * std::sin(age * std::numbers::pi)});
    }
    return puffs;
}
