#pragma once

#include "server/commands/QcCommandRegistry.h"

class QcServerState;
class QcServerWeather;

// /weather (decision E-395): everyone may ask for the weather, operators set it, for a number of
// seconds or as long as the world's course says (QcWeatherCycle). With the game rule weatherCycle
// off the set weather stays until /weather changes it again.
class QcWeatherCommands
{
public:
    QcWeatherCommands(QcServerState &state, QcServerWeather &weather);

    void addTo(QcCommandRegistry &registry);

private:
    void weather(const QcCommandContext &context);
    // Replies with the current weather (and how long it lasts while the cycle runs).
    void report(const QcCommandContext &context) const;

    QcServerState &m_state;
    QcServerWeather &m_weather;
};
