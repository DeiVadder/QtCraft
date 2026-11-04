// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/signal/QcDaylightSensors.h"

#include "common/content/blocks/QcPlacedBlock.h"
#include "common/world/QcDayCycle.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerState.h"
#include "server/signal/QcSignalPower.h"
#include "server/signal/QcSignalTable.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

using namespace Qt::StringLiterals;

QcDaylightSensors::QcDaylightSensors(QcServerState &state, QcServerBlocks &blocks, const QcSignalTable &table)
    : m_state(state)
    , m_blocks(blocks)
    , m_table(table)
{
}

int QcDaylightSensors::strength(int skyLight, int darkening, double sunAngle, bool inverted)
{
    int light = skyLight - darkening;
    if (inverted) {
        light = QcSignalDefinition::maxPower - light;
    } else if (light > 0) {
        const double noon = sunAngle < std::numbers::pi ? 0.0 : 2.0 * std::numbers::pi;
        const double eased = sunAngle + (noon - sunAngle) * 0.2;
        light = static_cast<int>(std::lround(light * std::cos(eased)));
    }
    return std::clamp(light, 0, QcSignalDefinition::maxPower);
}

int QcDaylightSensors::strengthAt(QcBlockPos pos, bool inverted) const
{
    const double worldTime = m_state.info().worldTime;
    const QcWeatherState &weather = m_state.info().weather;
    const int darkening = QcDayCycle::skyDarkening(worldTime, weather.rain, weather.thunder);
    return strength(m_state.world().skyLight(pos), darkening, QcDayCycle::sunAngle(worldTime), inverted);
}

void QcDaylightSensors::tick()
{
    const QcServerWorld &world = m_state.world();
    if (m_state.clock().tick() % QcSignalDefinition::daylightIntervalTicks != 0 || world.daylightSensors().size() == 0) {
        return;
    }

    // Decided first, set in a fixed order: the index has no order of its own and setting a block
    // writes to it. A change next to an unloaded chunk would wait there as a signal and ask for a
    // load ticket, so a sensor at the edge of the loaded area waits for its neighbours instead.
    const QcSignalPower around(world, m_table);
    std::vector<QcPlacedBlock> changes;
    for (const QcBlockPos &pos : world.daylightSensors().positions()) {
        const std::optional<QcBlockId> block = world.blockAt(pos);
        if (!block) {
            continue;
        }

        const QcSignalState &state = m_table.at(*block);
        const int power = strengthAt(pos, state.inverted);
        if (std::cmp_not_equal(power, state.power) && !around.unloadedNear(pos)) {
            changes.push_back({pos, m_table.content().withProperty(*block, u"power"_s, QString::number(power))});
        }
    }

    std::ranges::sort(changes, {}, &QcPlacedBlock::pos);
    for (const QcPlacedBlock &change : changes) {
        m_blocks.setAndBroadcast(change.pos, change.block);
    }
}

void QcDaylightSensors::turnRound(QcBlockPos pos, QcBlockId block)
{
    const bool inverted = !m_table.at(block).inverted;
    const QcBlockId turned = m_table.withFlag(block, u"inverted"_s, inverted);
    m_blocks.setAndBroadcast(pos, m_table.content().withProperty(turned, u"power"_s, QString::number(strengthAt(pos, inverted))));
}
