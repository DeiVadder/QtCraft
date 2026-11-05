// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcPlayerMessages.h"

class QcPlayerHealth;
class QcServerSounds;
class QcServerState;
struct QcServerClient;

/*! \brief Air under water in the server tick (decision E-955): players and creatures with their
    eyes under water lose their air (QcAirSupply) and then drown while the game rule
    drowningDamage holds; out of the water the air comes back.

    Spectators, creative players and the dead breathe freely, as do creatures that breathe under
    water (QcMobDefinition::breathesUnderWater: swimmers, divers, fish, golems) and dormant ones.
    A player hears of his air with a PlayerAir only when what his HUD shows changes. */
class QcDrowning
{
public:
    QcDrowning(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health);

    /*! The air of every playing player and every creature in a simulated tick. */
    void tick();

    /*! Returns whether the HUD of a player shows something else for \a now than for \a sent: other
        bubbles, a full breath gained or lost, the eyes under water or out of it. */
    [[nodiscard]] static bool differs(QcMsg::PlayerAir sent, QcMsg::PlayerAir now);

private:
    void tickPlayer(QcServerClient &client);
    void tickCreatures();
    // Tells `client` his air when the HUD would show something else than last told.
    static void sendAir(QcServerClient &client, bool submerged);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcPlayerHealth &m_health;
};
