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

} // namespace

void QcExplosionEffects::add(const QcMsg::Explosion &explosion, quint64 tick)
{
    const double power = std::clamp(static_cast<double>(explosion.power), 0.5, 16.0);
    const int smoke = static_cast<int>(std::ceil(power * smokePuffsPerPower));
    const int count = flashPuffs + smoke;
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
        puff.lifeTicks = flash ? 6 : 20 + static_cast<int>(unitOf(explosion.centre, index, 4) * 20.0);
        puff.size = (flash ? 0.5 : 0.25) * power * (0.6 + 0.4 * unitOf(explosion.centre, index, 5));
        puff.flash = flash;
        m_puffs.append(puff);
    }
}

void QcExplosionEffects::prune(quint64 tick)
{
    m_puffs.removeIf([tick](const Puff &puff) { return tick >= puff.startTick + static_cast<quint64>(puff.lifeTicks); });
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
