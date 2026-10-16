#pragma once

#include "QcItemDefinition.h"
#include "QcMessages.h"

// What the server last told the player about himself (QcClientSession::vitals): health and whether
// he burns or is dead, food, and experience (decision E-398).
class QcClientVitals
{
public:
    // Which of the vitals a message changed.
    enum class Change { None, Health, Food, Experience };

    // Takes over PlayerHealth, PlayerFood and PlayerExperience; Change::None for other messages.
    Change apply(const QcMessage &message);

    double health() const { return m_health.health; }
    double maxHealth() const { return m_health.maxHealth; }
    // The player is on fire (decision E-393): the HUD shows flames at the edge of the view.
    bool burning() const { return m_health.burning; }
    bool isDead() const { return m_health.dead; }
    int food() const { return m_food.food; }
    int maxFood() const { return m_food.maxFood; }
    // The experience level and the share of the way to the next one, 0..1 (the bar in the HUD).
    int experienceLevel() const { return m_experience.level; }
    int experiencePoints() const { return m_experience.points; }
    int experienceForNext() const { return m_experience.pointsForNext; }
    double experienceProgress() const { return double(m_experience.points) / double(m_experience.pointsForNext); }

private:
    QcMsg::PlayerHealth m_health{20.0, 20.0, false};
    QcMsg::PlayerFood m_food{QcItemDefinition::maxFood, QcItemDefinition::maxFood};
    QcMsg::PlayerExperience m_experience;
};
