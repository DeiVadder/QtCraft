#include "server/weather/QcServerWeather.h"

#include "common/physics/QcPhysics.h"
#include "server/core/QcServerClient.h"
#include "server/core/QcServerState.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

quint64 columnKey(int x, int z)
{
    return (quint64(quint32(x)) << 32) | quint32(z);
}

QcBlockPos feetOf(const QcServerClient &client)
{
    const QcVec3 &position = client.motion.position;
    return {int(std::floor(position.x)), int(std::floor(position.y)), int(std::floor(position.z))};
}

} // namespace

QcServerWeather::QcServerWeather(QcServerState &state)
    : m_state(state)
    , m_table(state.content())
    , m_meanStrikeTicks(std::max(1, int(std::lround(state.content().weather().lightning.meanSeconds / QcPhysics::tickDuration))))
{
}

const QcWeatherState &QcServerWeather::state() const
{
    return m_state.info().weather;
}

void QcServerWeather::tick()
{
    QcWorldInfo &info = m_state.info();
    info.weather = QcWeatherCycle::next(info.weather, info.seed, info.weatherCycle);
    strikeNearPlayers();
    syncClients();
}

void QcServerWeather::set(QcWeatherKind kind, std::optional<qint64> ticks)
{
    QcWorldInfo &info = m_state.info();
    info.weather = QcWeatherCycle::begun(info.weather, info.seed, kind, ticks);
}

QcWeather::Climate QcServerWeather::climateAt(int x, int z) const
{
    const quint64 key = columnKey(x, z);
    if (const auto it = m_climates.constFind(key); it != m_climates.constEnd()) {
        return *it;
    }

    // The cache only spares the generator's climate noise; it stays small around the players.
    if (m_climates.size() >= climateGeneration) {
        m_olderClimates = std::exchange(m_climates, {});
    }
    if (const auto it = m_olderClimates.constFind(key); it != m_olderClimates.constEnd()) {
        m_climates.insert(key, *it);
        return *it;
    }
    const QString biome = m_state.world().generator().biomeAt(x, z);
    const QcWeather::Climate climate = QcWeather::climateOf(biome.isEmpty() ? nullptr : m_state.content().worldgen().biome(biome));
    m_climates.insert(key, climate);
    return climate;
}

int QcServerWeather::columnTop(int x, int z) const
{
    const QcBlockPos column{x, 0, z};
    const std::shared_ptr<const QcChunk> chunk = m_state.world().chunk(column.chunk());
    return chunk ? m_table.top(*chunk, column.localX(), column.localZ()) : QcWorld::worldHeight;
}

QcPrecipitation QcServerWeather::precipitationAt(QcBlockPos pos) const
{
    if (state().rain < rainThreshold || !openToSky(pos)) {
        return QcPrecipitation::None;
    }
    return QcWeather::precipitationAt(climateAt(pos.x, pos.z), pos.y);
}

void QcServerWeather::strike(QcBlockPos pos)
{
    ++m_strikes;
    const QcVec3 at{pos.x + 0.5, double(pos.y), pos.z + 0.5};
    for (const auto &client : m_state.clients()) {
        const QcVec3 &position = client->motion.position;
        if (client->state == QcServerClient::State::Playing && std::hypot(position.x - at.x, position.z - at.z) <= strikeHearing) {
            client->connection->send(QcMsg::Lightning{at});
        }
    }

    if (m_strikeHook) {
        m_strikeHook(pos);
    }
}

void QcServerWeather::strikeNearPlayers()
{
    if (state().thunder < thunderThreshold) {
        return;
    }
    // Places of the strikes, drawn from the seed and the weather's tick.
    QcWeatherCycle::Draws draws(m_state.info().seed, state(), strikeRandom);

    QList<QcBlockPos> places;
    for (const auto &client : m_state.clients()) {
        if (client->state != QcServerClient::State::Playing || draws.bounded(m_meanStrikeTicks) != 0) {
            continue;
        }

        const QcBlockPos feet = feetOf(*client);
        const int x = feet.x + draws.bounded(-strikeReach, strikeReach + 1);
        const int z = feet.z + draws.bounded(-strikeReach, strikeReach + 1);
        const QcBlockPos top{x, columnTop(x, z) + 1, z};
        // Only into rain: not into snow, a dry biome or a column that is not loaded.
        if (top.isInsideWorldHeight() && precipitationAt(top) == QcPrecipitation::Rain) {
            places.append(top);
        }
    }

    // Struck after the loop: the hook may change the players.
    for (const QcBlockPos &place : places) {
        strike(place);
    }
}

QcMsg::Weather QcServerWeather::messageFor(const QcServerClient &client) const
{
    const QcBlockPos feet = feetOf(client);
    return {state().rain, state().thunder, climateAt(feet.x, feet.z)};
}

bool QcServerWeather::worthSending(const std::optional<QcMsg::Weather> &sent, QcMsg::Weather next)
{
    if (!sent) {
        return true;
    }

    const auto moved = [](float before, float now) {
        return std::abs(now - before) >= sendStep || (now != before && (now == 0.0f || now == 1.0f));
    };
    return sent->climate != next.climate || moved(sent->rain, next.rain) || moved(sent->thunder, next.thunder);
}

void QcServerWeather::sendTo(QcServerClient &client)
{
    const QcMsg::Weather message = messageFor(client);
    client.connection->send(message);
    client.sentWeather = message;
}

void QcServerWeather::syncClients()
{
    for (const auto &client : m_state.clients()) {
        if (client->state != QcServerClient::State::Playing) {
            continue;
        }

        const QcMsg::Weather next = messageFor(*client);
        if (worthSending(client->sentWeather, next)) {
            client->connection->send(next);
            client->sentWeather = next;
        }
    }
}
