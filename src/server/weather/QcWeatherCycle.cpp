// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/weather/QcWeatherCycle.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// A well-mixed 64-bit value of the seed, the spell and what it decides (splitmix64).
quint64 mix(qint64 seed, qint64 spell, quint64 salt)
{
    quint64 z = static_cast<quint64>(seed) ^ (static_cast<quint64>(spell) * 0x9E3779B97F4A7C15ull) ^ (salt * 0xD1B54A32D192ED03ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

constexpr quint64 kindSalt = 1;
constexpr quint64 lengthSalt = 2;
// Added to the purpose of QcWeatherCycle::Draws.
constexpr quint64 randomSalt = 16;

// Moves `level` one fade step towards `target`.
float fade(float level, float target)
{
    const float step = 1.0f / static_cast<float>(QcWeatherCycle::fadeTicks);
    if (level < target) {
        return std::min(target, level + step);
    }
    return std::max(target, level - step);
}

} // namespace

namespace QcWeatherCycle {

QcWeatherKind nextKind(qint64 seed, qint64 spell, QcWeatherKind previous)
{
    if (previous != QcWeatherKind::Clear) {
        return QcWeatherKind::Clear;
    }
    return mix(seed, spell, kindSalt) % 100 < static_cast<quint64>(thunderChance) ? QcWeatherKind::Thunder : QcWeatherKind::Rain;
}

qint64 lengthOf(qint64 seed, qint64 spell, QcWeatherKind kind)
{
    qint64 shortest = minClearTicks;
    qint64 longest = maxClearTicks;
    if (kind == QcWeatherKind::Rain) {
        shortest = minRainTicks;
        longest = maxRainTicks;
    } else if (kind == QcWeatherKind::Thunder) {
        shortest = minThunderTicks;
        longest = maxThunderTicks;
    }
    return shortest + static_cast<qint64>(mix(seed, spell, lengthSalt) % static_cast<quint64>(longest - shortest + 1));
}

Draws::Draws(qint64 seed, const QcWeatherState &state, quint64 purpose)
    : m_state(mix(seed, state.ticks, randomSalt + purpose))
{
}

int Draws::bounded(int lowest, int highest)
{
    m_state += 0x9E3779B97F4A7C15ull;
    quint64 z = m_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return lowest + static_cast<int>(z % static_cast<quint64>(highest - lowest));
}

QcWeatherState next(QcWeatherState state, qint64 seed, bool cycle)
{
    ++state.ticks;
    if (state.spell == 0) {
        state.kind = QcWeatherKind::Clear;
        state.ticksLeft = lengthOf(seed, 0, QcWeatherKind::Clear);
        state.spell = 1;
    }

    if (cycle && --state.ticksLeft <= 0) {
        state.kind = nextKind(seed, state.spell, state.kind);
        state.ticksLeft = lengthOf(seed, state.spell, state.kind);
        ++state.spell;
    }

    state.rain = fade(state.rain, state.kind == QcWeatherKind::Clear ? 0.0f : 1.0f);
    state.thunder = fade(state.thunder, state.kind == QcWeatherKind::Thunder ? 1.0f : 0.0f);
    return state;
}

QcWeatherState begun(QcWeatherState state, qint64 seed, QcWeatherKind kind, std::optional<qint64> ticks)
{
    state.kind = kind;
    state.ticksLeft = std::clamp<qint64>(ticks.value_or(lengthOf(seed, state.spell, kind)), 1, maxSetTicks);
    ++state.spell;
    return state;
}

QJsonObject toJson(const QcWeatherState &state)
{
    return {{u"kind"_s, QcWeather::name(state.kind)},
            {u"ticksLeft"_s, state.ticksLeft},
            {u"spell"_s, state.spell},
            {u"rain"_s, static_cast<double>(state.rain)},
            {u"thunder"_s, static_cast<double>(state.thunder)},
            {u"ticks"_s, state.ticks}};
}

QcWeatherState fromJson(const QJsonObject &json)
{
    QcWeatherState state;
    const std::optional<QcWeatherKind> kind = QcWeather::parse(json.value(u"kind"_s).toString());
    const qint64 spell = json.value(u"spell"_s).toInteger(0);
    if (!kind || spell <= 0) {
        return state;
    }

    // Negated comparisons, so NaN becomes 0.
    const auto strength = [&json](const QString &key) {
        const double value = json.value(key).toDouble();
        return static_cast<float>(value > 0.0 ? std::min(value, 1.0) : 0.0);
    };
    state.kind = *kind;
    state.spell = spell;
    state.ticksLeft = std::clamp<qint64>(json.value(u"ticksLeft"_s).toInteger(1), 1, maxSetTicks);
    state.rain = strength(u"rain"_s);
    state.thunder = strength(u"thunder"_s);
    state.ticks = std::max<qint64>(json.value(u"ticks"_s).toInteger(0), 0);
    return state;
}

} // namespace QcWeatherCycle
