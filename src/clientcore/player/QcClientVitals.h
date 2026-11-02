// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcItemDefinition.h"
#include "common/gameplay/QcAirSupply.h"
#include "common/protocol/QcMessages.h"

/*! \brief What the server last told the player about themselves (QcClientSession::vitals): health
    and whether they burn or are dead, food, experience (decision E-398) and air under water
    (decision E-955). */
class QcClientVitals
{
public:
    /*! \brief Which of the vitals a message changed. */
    enum class Change {
        None,       // nothing
        Health,     // health, burning, dead or the effects
        Food,       // food
        Experience, // experience
        Air         // air under water
    };

    /*! Takes over \a message when it is PlayerHealth, PlayerFood, PlayerExperience or PlayerAir.
        Returns what changed; Change::None for other messages. */
    Change apply(const QcMessage &message);

    /*! Returns whether the server told the health at least once (before, the vitals are the full
        defaults). */
    bool healthKnown() const { return m_healthKnown; }
    /*! Returns the player's health in points. */
    double health() const { return m_health.health; }
    /*! Returns the most health the player can have. */
    double maxHealth() const { return m_health.maxHealth; }
    /*! Returns whether the player is on fire (decision E-393): the HUD shows flames at the edge
        of the view. */
    bool burning() const { return m_health.burning; }
    /*! Returns whether the player is dead. */
    bool isDead() const { return m_health.dead; }
    /*! Returns the status effects with their ticks left when the server last told (decision
        E-450). */
    const QList<QcStatusEffect> &effects() const { return m_health.effects; }
    /*! Returns the player's food points. */
    int food() const { return m_food.food; }
    /*! Returns the most food points. */
    int maxFood() const { return m_food.maxFood; }
    /*! Returns the experience level. */
    int experienceLevel() const { return m_experience.level; }
    /*! Returns the experience points within the current level. */
    int experiencePoints() const { return m_experience.points; }
    /*! Returns the points the current level needs for the next one. */
    int experienceForNext() const { return m_experience.pointsForNext; }
    /*! Returns the share of the way to the next level, 0..1 (the bar in the HUD). */
    double experienceProgress() const { return static_cast<double>(m_experience.points) / static_cast<double>(m_experience.pointsForNext); }
    /*! Returns the share of a full breath left, 0..1, as the server last told (in steps of a
        bubble). */
    double air() const { return static_cast<double>(m_air.air) / static_cast<double>(m_air.maxAir); }
    /*! Returns the bubbles of the air left (QcAirSupply::bubblesOf). */
    int airBubbles() const { return QcAirSupply::bubblesOf(m_air.air, m_air.maxAir); }
    /*! Returns whether the HUD shows the air: with the eyes under water and until the breath is
        full again. */
    bool airShown() const { return m_air.submerged || m_air.air < m_air.maxAir; }
    /*! Returns whether the last PlayerAir took a bubble under water (the client plays a pop). */
    bool bubblePopped() const { return m_bubblePopped; }

private:
    QcMsg::PlayerHealth m_health{20.0, 20.0, false};
    bool m_healthKnown = false;
    QcMsg::PlayerFood m_food{QcItemDefinition::maxFood, QcItemDefinition::maxFood};
    QcMsg::PlayerExperience m_experience;
    QcMsg::PlayerAir m_air{QcAirSupply::maxAir, QcAirSupply::maxAir, false};
    bool m_airKnown = false;
    bool m_bubblePopped = false;
};
