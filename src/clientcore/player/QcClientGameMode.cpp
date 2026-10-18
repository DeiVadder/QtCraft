#include "clientcore/player/QcClientGameMode.h"

void QcClientGameMode::setMode(QcGameMode mode)
{
    m_mode = mode;
    if (!rules().mayFly) {
        m_flightWanted = false;
    }
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
