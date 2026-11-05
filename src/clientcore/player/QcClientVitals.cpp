// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcClientVitals.h"

QcClientVitals::Change QcClientVitals::apply(const QcMessage &message)
{
    if (const auto *health = std::get_if<QcMsg::PlayerHealth>(&message)) {
        m_health = *health;
        m_healthKnown = true;
        return Change::Health;
    }
    if (const auto *food = std::get_if<QcMsg::PlayerFood>(&message)) {
        m_food = *food;
        return Change::Food;
    }
    if (const auto *experience = std::get_if<QcMsg::PlayerExperience>(&message)) {
        m_experience = *experience;
        return Change::Experience;
    }
    if (const auto *air = std::get_if<QcMsg::PlayerAir>(&message)) {
        // The first air after joining only tells the state: nothing went under water just now.
        m_bubblePopped = m_airKnown && air->submerged && QcAirSupply::bubblesOf(air->air, air->maxAir) < airBubbles();
        m_air = *air;
        m_airKnown = true;
        return Change::Air;
    }
    return Change::None;
}
