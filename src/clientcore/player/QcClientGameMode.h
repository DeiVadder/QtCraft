#pragma once

#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcAdventureRules.h"
#include "common/gameplay/QcGameMode.h"
#include "common/protocol/QcMessages.h"

#include <functional>
#include <optional>
#include <utility>

// The local player's game mode as the server reported it (decision E-282), with the world's
// adventure allow-lists in adventure mode (decision E-735), and what the client does with it: the
// wish to fly that goes with every input (decision E-284), the pace of instant breaking
// in creative mode, and the requests for catalogue items (E-283) and picked blocks (E-285).
class QcClientGameMode
{
public:
    // While the mine button is held in creative mode, the next block breaks this many ticks after
    // the previous one (the model waits about as long).
    static constexpr int instantBreakTicks = 5;

    using Sender = std::function<void(const QcMessage &message)>;
    // Takes the player's wish to fly to where the inputs are made (the session's tick core).
    using WishListener = std::function<void(bool wanted)>;

    // How requests reach the server (the session's connection).
    void setSender(Sender sender) { m_send = std::move(sender); }
    void setWishListener(WishListener listener) { m_wishListener = std::move(listener); }
    QcGameMode mode() const { return m_mode; }
    QcGameModes::Rules rules() const { return QcGameModes::rules(m_mode); }
    void setMode(QcGameMode mode);
    // The allow-lists that came with the mode (empty outside adventure mode).
    const QcAdventureRules &adventure() const { return m_adventure; }
    void setAdventure(QcAdventureRules adventure) { m_adventure = std::move(adventure); }

    // Double tap of jump: starts or ends a flight where the mode allows flying.
    void toggleFlight();
    // Sets the wish directly (control interface); tells the wish listener.
    void setFlightWanted(bool wanted);
    // Takes the wish as the tick core holds it, without telling the listener (decision E-560).
    void mirrorFlight(bool wanted) { m_flightWanted = wanted; }
    // The wish sent with the next input.
    bool flightWanted() const { return m_flightWanted; }
    // After each predicted step: a landing or a mode without flight ended the flight, so the wish
    // follows the predicted state.
    void stepped(bool flying) { m_flightWanted = flying; }

    // Instant breaking: the block to break this tick (or none), given the block the player mines
    // (none while the button is up). The first strike breaks at once, holding repeats every
    // instantBreakTicks.
    std::optional<QcBlockPos> nextInstantBreak(std::optional<QcBlockPos> mined);
    // The button went up: the next press is a first strike again, even before the next tick.
    void releaseInstantBreak() { m_breakCooldown = 0; }

    // A spectator's flight speed (QcPlayerInput::flySpeed) after `steps` notches of the mouse wheel
    // (up is faster), within its range (decision E-730).
    [[nodiscard]] static quint8 steppedFlySpeed(quint8 speed, int steps);

    // The creative catalogue: puts `stack` into `slot` (QcMsg::CreativeItem); the server refuses it
    // outside creative mode.
    void creativeItem(int slot, const QcItemStack &stack) const;
    // The middle mouse button on the block at `pos` (QcMsg::PickBlock).
    void pickBlock(QcBlockPos pos) const;

private:
    Sender m_send;
    WishListener m_wishListener;
    QcGameMode m_mode = QcGameMode::Survival;
    QcAdventureRules m_adventure;
    bool m_flightWanted = false;
    int m_breakCooldown = 0;
};
