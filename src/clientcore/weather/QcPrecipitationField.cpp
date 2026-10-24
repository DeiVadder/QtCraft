#include "clientcore/weather/QcPrecipitationField.h"

#include "clientcore/session/QcClientWorld.h"
#include "clientcore/weather/QcClientWeather.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

quint64 columnKey(int x, int z)
{
    return (static_cast<quint64>(static_cast<quint32>(x)) << 32) | static_cast<quint32>(z);
}

// Columns tried for one new particle before giving up (a field under a roof stays empty quickly).
constexpr int spawnAttempts = 4;

} // namespace

QcPrecipitationField::QcPrecipitationField(int budget)
    : m_budget(std::clamp(budget, 0, maxBudget))
{
}

int QcPrecipitationField::selectedBudget()
{
    bool ok = false;
    const int wanted = qEnvironmentVariableIntValue("QTCRAFT_WEATHER_PARTICLES", &ok);
    return ok ? std::clamp(wanted, 0, maxBudget) : defaultBudget;
}

void QcPrecipitationField::ensureTable(const QcClientWorld &world)
{
    if (m_tableContent != &world.content()) {
        m_table = std::make_shared<const QcPrecipitationTable>(world.content());
        m_tableContent = &world.content();
        m_columnTops.clear();
    }
}

int QcPrecipitationField::columnTop(const QcClientWorld &world, int x, int z)
{
    ensureTable(world);
    const quint64 key = columnKey(x, z);
    if (const auto it = m_columnTops.constFind(key); it != m_columnTops.constEnd()) {
        return *it;
    }

    const QcBlockPos column{x, 0, z};
    const std::shared_ptr<const QcChunk> chunk = world.chunk(column.chunk());
    const int top = chunk ? m_table->top(*chunk, column.localX(), column.localZ()) : QcWorld::worldHeight;
    m_columnTops.insert(key, top);
    return top;
}

std::optional<QcPrecipitationField::Particle> QcPrecipitationField::spawn(const QcClientWorld &world, const QcClientWeather &weather,
                                                                          const QcVec3 &eye, bool anywhere)
{
    const double ceiling = eye.y + reachUp;
    for (int attempt = 0; attempt < spawnAttempts; ++attempt) {
        const int x = static_cast<int>(std::floor(eye.x)) + m_random.bounded(-radius, radius + 1);
        const int z = static_cast<int>(std::floor(eye.z)) + m_random.bounded(-radius, radius + 1);
        const double floor = std::max(static_cast<double>(columnTop(world, x, z) + 1), eye.y - reachDown);
        if (floor >= ceiling) {
            continue;
        }

        const double y = anywhere ? floor + m_random.generateDouble() * (ceiling - floor) : ceiling - m_random.generateDouble();
        const QcPrecipitation falling = weather.precipitationAt(static_cast<int>(std::floor(y)));
        if (falling == QcPrecipitation::None) {
            return std::nullopt;
        }
        const QcVec3 position{x + m_random.generateDouble(), std::max(y, floor), z + m_random.generateDouble()};
        return Particle{position, falling == QcPrecipitation::Snow, static_cast<float>(m_random.generateDouble() * 2.0 * std::numbers::pi)};
    }
    return std::nullopt;
}

bool QcPrecipitationField::ended(const QcClientWorld &world, const Particle &particle, const QcVec3 &eye)
{
    const QcVec3 &p = particle.position;
    if (std::abs(p.x - eye.x) > radius + 1 || std::abs(p.z - eye.z) > radius + 1 || p.y < eye.y - reachDown) {
        return true;
    }
    return p.y < columnTop(world, static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.z))) + 1;
}

void QcPrecipitationField::update(const QcClientWorld &world, const QcClientWeather &weather, const QcVec3 &eye, double seconds)
{
    const double dt = std::clamp(seconds, 0.0, 0.1);
    m_time += dt;
    m_sinceRefresh += dt;
    if (m_sinceRefresh >= columnRefreshSeconds) {
        m_columnTops.clear();
        m_sinceRefresh = 0.0;
    }

    // Particles that landed or left the box start again at the top; new ones (a shower beginning,
    // the strength growing) anywhere in the box, so slow snow does not begin as one falling layer.
    const auto wanted = static_cast<qsizetype>(std::lround(static_cast<double>(m_budget) * std::clamp(static_cast<double>(weather.rain()), 0.0, 1.0)));
    for (qsizetype i = 0; i < m_particles.size();) {
        Particle &particle = m_particles[i];
        if (particle.snow) {
            particle.position.y -= snowSpeed * dt;
            particle.position.x += 0.35 * std::sin(m_time * 1.3 + particle.phase) * dt;
            particle.position.z += 0.35 * std::cos(m_time * 1.1 + particle.phase) * dt;
        } else {
            particle.position.y -= rainSpeed * dt;
            particle.position.x += rainDrift * dt;
        }

        if (!ended(world, particle, eye)) {
            ++i;
            continue;
        }
        if (const std::optional<Particle> next = m_particles.size() <= wanted ? spawn(world, weather, eye, false) : std::nullopt) {
            particle = *next;
            ++i;
        } else {
            // The last particle takes this place (not yet moved this frame), so removing costs O(1).
            particle = m_particles.constLast();
            m_particles.removeLast();
        }
    }

    for (qsizetype added = 0; m_particles.size() < wanted && added < m_budget / 8 + 1; ++added) {
        if (const std::optional<Particle> next = spawn(world, weather, eye, true)) {
            m_particles.append(*next);
        }
    }
}

QcPrecipitationDrop QcPrecipitationField::dropOf(const Particle &particle, float strength, const QcVec3 &origin)
{
    const float shown = std::clamp(strength, 0.0f, 1.0f);
    const QcVec3 p = particle.position - origin;
    const auto alpha = static_cast<quint8>(particle.snow ? shown * 190.0f + 40.0f : shown * 150.0f + 40.0f);
    return {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z), static_cast<quint8>(particle.snow ? 1 : 0), alpha, {0, 0}};
}
