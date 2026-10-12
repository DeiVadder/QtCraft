#pragma once

#include "QcBlockPos.h"
#include "QcGameMode.h"
#include "QcMessages.h"

#include <functional>
#include <optional>

// The local player's game mode as the server reported it (decision E-282) and what the client does
// with it: the wish to fly that goes with every input (decision E-284), the pace of instant breaking
// in creative mode, and the requests for catalogue items (E-283) and picked blocks (E-285).
class QcClientGameMode
{
public:
    // While the mine button is held in creative mode, the next block breaks this many ticks after
    // the previous one (the model waits about as long).
    static constexpr int instantBreakTicks = 5;

    using Sender = std::function<void(const QcMessage &message)>;

    // How requests reach the server (the session's connection).
    void setSender(Sender sender) { m_send = std::move(sender); }
    QcGameMode mode() const { return m_mode; }
    QcGameModes::Rules rules() const { return QcGameModes::rules(m_mode); }
    void setMode(QcGameMode mode);

    // Double tap of jump: starts or ends a flight where the mode allows flying.
    void toggleFlight();
    // Sets the wish directly (control interface).
    void setFlightWanted(bool wanted);
    // The wish sent with the next input.
    bool flightWanted() const { return m_flightWanted; }
    // After each predicted step: a landing or a mode without flight ended the flight, so the wish
    // follows the predicted state.
    void stepped(bool flying) { m_flightWanted = flying; }

    // Instant breaking: the block to break this tick (or none), given the block the player mines
    // (none while the button is up). The first strike breaks at once, holding repeats every
    // instantBreakTicks.
    std::optional<QcBlockPos> nextInstantBreak(std::optional<QcBlockPos> mined);

    // The creative catalogue: puts `stack` into `slot` (QcMsg::CreativeItem); the server refuses it
    // outside creative mode.
    void creativeItem(int slot, QcItemStack stack) const;
    // The middle mouse button on the block at `pos` (QcMsg::PickBlock).
    void pickBlock(QcBlockPos pos) const;

private:
    Sender m_send;
    QcGameMode m_mode = QcGameMode::Survival;
    bool m_flightWanted = false;
    int m_breakCooldown = 0;
};
