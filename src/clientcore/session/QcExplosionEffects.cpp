// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcExplosionEffects.h"

#include "common/physics/QcPhysics.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

// A number in [0, 1) for puff `index` of the explosion at `centre` and its `salt`.
double unitOf(const QcVec3 &centre, int index, quint32 salt)
{
    quint64 h = static_cast<quint64>(static_cast<qint64>(std::floor(centre.x * 8.0))) * 0x9E3779B97F4A7C15ull;
    h ^= static_cast<quint64>(static_cast<qint64>(std::floor(centre.y * 8.0))) * 0xC2B2AE3D27D4EB4Full;
    h ^= static_cast<quint64>(static_cast<qint64>(std::floor(centre.z * 8.0))) * 0x165667B19E3779F9ull;
    h += static_cast<quint64>(index) * 0x27D4EB2F165667C5ull + salt;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
    h ^= h >> 31;
    return static_cast<double>(h >> 11) / static_cast<double>(1ull << 53);
}

// A direction spread over the sphere, a little more upwards.
QcVec3 directionOf(const QcVec3 &centre, int index)
{
    const double angle = 2.0 * std::numbers::pi * unitOf(centre, index, 1);
    const double rise = unitOf(centre, index, 2) * 1.6 - 0.6;
    const double flat = std::sqrt(std::max(0.0, 1.0 - std::min(1.0, rise * rise)));
    return {std::cos(angle) * flat, rise, std::sin(angle) * flat};
}

// 1 at the centre, falling to 0 at `reach`.
double closeness(const QcVec3 &eye, const QcVec3 &centre, double reach)
{
    const double distance = (eye - centre).length();
    return reach > 0.0 ? std::clamp(1.0 - distance / reach, 0.0, 1.0) : 0.0;
}

} // namespace

void QcExplosionEffects::add(const QcMsg::Explosion &explosion, quint64 tick)
{
    const double power = std::clamp(static_cast<double>(explosion.power), 0.5, 16.0);
    const int smoke = static_cast<int>(std::ceil(power * smokePuffsPerPower));
    const int count = flashPuffs + smoke;
    // A chain of many blasts shakes and flashes as one: the strongest counts, not the sum. When the
    // list is full, the new blast takes the place of the weakest, the oldest among equals.
    if (m_blasts.size() >= maxBlasts) {
        const auto weakest = std::ranges::min_element(m_blasts, [](const Blast &a, const Blast &b) { return a.power != b.power ? a.power < b.power : a.tick < b.tick; });
        m_blasts.erase(weakest);
    }
    m_blasts.append({explosion.centre, power, tick});
    if (m_puffs.size() + count > maxPuffs) {
        return;
    }

    for (int index = 0; index < count; ++index) {
        const bool flash = index < flashPuffs;
        const double speed = (flash ? 3.0 : 1.5) * power * (0.5 + unitOf(explosion.centre, index, 3));
        Puff puff;
        puff.centre = explosion.centre;
        puff.velocity = directionOf(explosion.centre, index) * speed;
        puff.startTick = tick;
        puff.lifeTicks = flash ? 6 + static_cast<int>(unitOf(explosion.centre, index, 4) * 4.0) : 20 + static_cast<int>(unitOf(explosion.centre, index, 4) * 30.0);
        puff.size = (flash ? 0.5 : 0.25) * power * (0.6 + 0.4 * unitOf(explosion.centre, index, 5));
        puff.flash = flash;
        if (index == 0) {
            // The fireball: one large flash that stays at the centre.
            puff.velocity = {};
            puff.size = 0.6 * power;
            puff.lifeTicks = 5;
        }
        m_puffs.append(puff);
    }
}

void QcExplosionEffects::prune(quint64 tick)
{
    m_puffs.removeIf([tick](const Puff &puff) { return tick >= puff.startTick + static_cast<quint64>(puff.lifeTicks); });
    m_blasts.removeIf([tick](const Blast &blast) { return tick >= blast.tick + static_cast<quint64>(std::max(flashTicks, shakeTicks)); });
}

double QcExplosionEffects::flashAt(const QcVec3 &eye, double ticks) const
{
    double flash = 0.0;
    for (const Blast &blast : m_blasts) {
        const double age = ticks - static_cast<double>(blast.tick);
        if (age < 0.0 || age >= flashTicks) {
            continue;
        }
        const double fade = 1.0 - age / flashTicks;
        flash = std::max(flash, closeness(eye, blast.centre, flashReachPerPower * blast.power) * fade * fade);
    }
    return flash;
}

double QcExplosionEffects::shakeAt(const QcVec3 &eye, double ticks) const
{
    double strength = 0.0;
    for (const Blast &blast : m_blasts) {
        const double age = ticks - static_cast<double>(blast.tick);
        if (age < 0.0 || age >= shakeTicks) {
            continue;
        }
        // Strong at once, then dying away over shakeTicks.
        const double near = closeness(eye, blast.centre, shakeReachPerPower * blast.power);
        const double fade = std::pow(1.0 - age / shakeTicks, 2.0);
        strength = std::max(strength, near * near * fade);
    }
    return strength;
}

QcExplosionEffects::Shake QcExplosionEffects::shake(double strength, double ticks)
{
    // Sines of unrelated frequencies make an irregular tremble that is smooth from frame to frame.
    const double seconds = ticks * QcPhysics::tickDuration;
    const double yaw = std::sin(seconds * 47.0) * 0.6 + std::sin(seconds * 71.0 + 1.3) * 0.4;
    const double pitch = std::sin(seconds * 53.0 + 0.7) * 0.6 + std::sin(seconds * 83.0 + 2.1) * 0.4;
    const double degrees = std::clamp(strength, 0.0, 1.0) * maxShakeDegrees;
    return {yaw * degrees, pitch * degrees};
}

std::optional<QcExplosionEffects::Look> QcExplosionEffects::lookAt(const Puff &puff, double ticks)
{
    const double age = ticks - static_cast<double>(puff.startTick);
    if (age < 0.0 || age >= puff.lifeTicks) {
        return std::nullopt;
    }

    // The burst slows down quickly; smoke rises a little on top.
    const double seconds = age * QcPhysics::tickDuration;
    const double travelled = 1.0 - std::exp(-seconds * 4.0);
    const double rise = puff.flash ? 0.0 : seconds * 0.8;
    const double left = 1.0 - age / puff.lifeTicks;
    Look look;
    look.position = puff.centre + puff.velocity * (travelled / 4.0) + QcVec3{0.0, rise, 0.0};
    look.size = puff.size * (puff.flash ? 0.5 + left : 0.4 + 0.6 * left);
    look.flash = puff.flash;
    return look;
}
