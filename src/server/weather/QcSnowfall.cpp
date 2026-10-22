#include "server/weather/QcSnowfall.h"

#include "common/blockrules/QcBlockRules.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerState.h"
#include "server/weather/QcServerWeather.h"
#include "server/weather/QcWeatherCycle.h"

#include <cmath>

QcSnowfall::QcSnowfall(QcServerState &state, QcServerBlocks &blocks, const QcServerWeather &weather)
    : m_state(state)
    , m_blocks(blocks)
    , m_weather(weather)
    , m_layer(state.content().blockId(state.content().weather().snow.layer))
{
}

void QcSnowfall::tick()
{
    const int columns = m_state.content().weather().snow.columnsPerTick;
    const std::optional<QcBlockId> layer = m_layer;
    if (!layer || columns <= 0 || m_weather.state().rain < QcServerWeather::rainThreshold) {
        return;
    }
    // The columns, drawn from the seed and the weather's tick, apart from the strikes (QcServerWeather).
    QcWeatherCycle::Draws draws(m_state.info().seed, m_weather.state(), snowRandom);

    for (const auto &client : m_state.clients()) {
        if (client->state != QcServerClient::State::Playing) {
            continue;
        }

        const QcVec3 &position = client->motion.position;
        const int px = int(std::floor(position.x));
        const int pz = int(std::floor(position.z));
        for (int i = 0; i < columns; ++i) {
            const int x = px + draws.bounded(-reach, reach + 1);
            const int z = pz + draws.bounded(-reach, reach + 1);
            ++m_columns;
            m_layers += settle(x, z, *layer) ? 1 : 0;
        }
    }
}

bool QcSnowfall::settle(int x, int z, QcBlockId layer)
{
    const int top = m_weather.columnTop(x, z);
    const QcBlockPos pos{x, top + 1, z};
    if (top < 0 || !pos.isInsideWorldHeight() || !m_weather.isSnowingAt(pos)) {
        return false;
    }

    const QcServerWorld &world = m_state.world();
    const std::optional<QcBlockId> ground = world.blockAt({x, top, z});
    const QcContentRegistry &content = m_state.content();
    if (world.blockAt(pos) != qcAirBlockId || !ground || !content.isValidBlock(*ground) || !content.state(*ground).opaque
        || !QcBlockRules::isSupported(world, pos, layer)) {
        return false;
    }

    m_blocks.setAndBroadcast(pos, layer);
    return world.blockAt(pos) == layer;
}
