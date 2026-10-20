#include "clientcore/player/QcClientGameMode.h"

#include "common/physics/QcPlayerInput.h"

#include <algorithm>

void QcClientGameMode::setMode(QcGameMode mode)
{
    m_mode = mode;
    if (!rules().mayFly) {
        m_flightWanted = false;
    }
}

quint8 QcClientGameMode::steppedFlySpeed(quint8 speed, int steps)
{
    return quint8(std::clamp(int(speed) + steps, int(QcPlayerInput::minFlySpeed), int(QcPlayerInput::maxFlySpeed)));
}

void QcClientGameMode::creativeItem(int slot, QcItemStack stack) const
{
    if (m_send) {
        m_send(QcMsg::CreativeItem{slot, stack});
    }
}

void QcClientGameMode::pickBlock(QcBlockPos pos) const
{
    if (m_send) {
        m_send(QcMsg::PickBlock{pos});
    }
}

void QcClientGameMode::toggleFlight()
{
    setFlightWanted(!m_flightWanted);
}

void QcClientGameMode::setFlightWanted(bool wanted)
{
    m_flightWanted = wanted && rules().mayFly;
    if (m_wishListener) {
        m_wishListener(m_flightWanted);
    }
}

std::optional<QcBlockPos> QcClientGameMode::nextInstantBreak(std::optional<QcBlockPos> mined)
{
    if (!mined) {
        m_breakCooldown = 0;
        return std::nullopt;
    }

    if (m_breakCooldown > 0) {
        --m_breakCooldown;
        return std::nullopt;
    }

    m_breakCooldown = instantBreakTicks - 1;
    return mined;
}
