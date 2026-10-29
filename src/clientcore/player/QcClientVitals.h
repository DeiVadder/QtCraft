// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcItemDefinition.h"
#include "common/protocol/QcMessages.h"

// What the server last told the player about himself (QcClientSession::vitals): health and whether
// he burns or is dead, food, and experience (decision E-398).
class QcClientVitals
{
public:
    // Which of the vitals a message changed.
    enum class Change { None, Health, Food, Experience };

    // Takes over PlayerHealth, PlayerFood and PlayerExperience; Change::None for other messages.
    Change apply(const QcMessage &message);

    // The server told the health at least once (before, the vitals are the full defaults).
    bool healthKnown() const { return m_healthKnown; }
    double health() const { return m_health.health; }
    double maxHealth() const { return m_health.maxHealth; }
    // The player is on fire (decision E-393): the HUD shows flames at the edge of the view.
    bool burning() const { return m_health.burning; }
    bool isDead() const { return m_health.dead; }
    // The status effects with their ticks left when the server last told (decision E-450).
    const QList<QcStatusEffect> &effects() const { return m_health.effects; }
    int food() const { return m_food.food; }
    int maxFood() const { return m_food.maxFood; }
    // The experience level and the share of the way to the next one, 0..1 (the bar in the HUD).
    int experienceLevel() const { return m_experience.level; }
    int experiencePoints() const { return m_experience.points; }
    int experienceForNext() const { return m_experience.pointsForNext; }
    double experienceProgress() const { return static_cast<double>(m_experience.points) / static_cast<double>(m_experience.pointsForNext); }

private:
    QcMsg::PlayerHealth m_health{20.0, 20.0, false};
    bool m_healthKnown = false;
    QcMsg::PlayerFood m_food{QcItemDefinition::maxFood, QcItemDefinition::maxFood};
    QcMsg::PlayerExperience m_experience;
};
