// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcItemDefinition.h"

/*! \brief Food points and the hidden saturation of one player (decisions E-039, E-1001).

    Activity adds exhaustion; every exhaustionPerFood of it costs one point of saturation while
    there is any, else one food point. Eating gives food points and saturation, which never
    exceeds the food points. With a full bar and saturation left the player heals quickly
    (fastRegenerationInterval), paying the exhaustion from the saturation; otherwise with at least
    regenerationFood points he heals one health point every regenerationInterval seconds, which
    exhausts as well; at zero food the player loses one health point every starvationInterval
    seconds, but never below starvationFloor. */
class QcHunger
{
public:
    /*! Food points of a full bar. */
    static constexpr int maxFood = QcItemDefinition::maxFood;
    /*! Food points at least for regeneration. */
    static constexpr int regenerationFood = 18;
    /*! Exhaustion that costs one point of saturation or food. */
    static constexpr double exhaustionPerFood = 4.0;
    /*! Seconds between two health points of regeneration. */
    static constexpr double regenerationInterval = 4.0;
    /*! Seconds between two steps of the fast regeneration at a full bar with saturation. */
    static constexpr double fastRegenerationInterval = 0.5;
    /*! Exhaustion one health point of the slow regeneration costs (2 as in E-100, the
        reference's 6 only for the fast regeneration; decision E-1001). */
    static constexpr double regenerationExhaustion = 2.0;
    /*! Exhaustion one health point of the fast regeneration costs (the reference's 6: a step
        heals min(saturation, 6) / 6). */
    static constexpr double fastRegenerationExhaustion = 6.0;
    /*! Seconds between two hits of starvation at zero food. */
    static constexpr double starvationInterval = 4.0;
    /*! Health one hit of starvation takes. */
    static constexpr double starvationDamage = 1.0;
    /*! Starvation never takes health below this. */
    static constexpr double starvationFloor = 1.0;
    /*! Saturation of a new and of a respawned player (the reference's 5). */
    static constexpr double spawnSaturation = QcItemDefinition::spawnSaturation;

    /*! Exhaustion per activity (decisions E-100, E-1001). Movement keeps the slower pace of
        E-100: walking empties a full bar in about two hours, sprinting in about eight minutes,
        the saturation of a meal comes on top. Fights and hurts cost the reference's values.
        \qcshared */
    static constexpr double walkPerMetre = 0.0025;
    static constexpr double sprintPerMetre = 0.03;
    static constexpr double swimPerMetre = 0.005;
    static constexpr double jump = 0.02;
    static constexpr double sprintJump = 0.08;
    static constexpr double mineBlock = 0.005;
    static constexpr double attack = 0.1;
    static constexpr double damageTaken = 0.1;

    /*! Returns the minutes of steady movement at \a speed metres per second with \a perMetre
        exhaustion that empty a full food bar without saturation. */
    static constexpr double minutesForFullBar(double perMetre, double speed)
    {
        return maxFood * exhaustionPerFood / (perMetre * speed) / 60.0;
    }

    /*! Returns the food points. */
    int food() const { return m_food; }
    /*! Returns the saturation, from 0 to food(). */
    double saturation() const { return m_saturation; }
    /*! Returns the exhaustion gathered towards the next point of saturation or food. */
    double exhaustion() const { return m_exhaustion; }
    /*! Takes \a food, \a exhaustion and \a saturation from a save, clamped to their ranges
        (the saturation to the food points). */
    void restore(int food, double exhaustion, double saturation);
    /*! Fills the bar, sets the saturation of a respawned player and clears the exhaustion and
        timers. */
    void reset();

    /*! Sets whether it is \a frozen: while frozen (creative mode) neither exhaustion nor the
        timers change anything. */
    void setFrozen(bool frozen) { m_frozen = frozen; }
    /*! Returns whether it is frozen. */
    bool frozen() const { return m_frozen; }

    /*! Adds \a amount of exhaustion (nothing while frozen). */
    void exhaust(double amount);
    /*! Returns the exhaustion of one movement step: \a distance metres moved horizontally,
        \a sprinting, \a inWater, and whether the player \a jumped. */
    static double movementExhaustion(double distance, bool sprinting, bool inWater, bool jumped);
    /*! Eats food of \a points with the saturation \a modifier of the item (points × modifier × 2
        saturation, like the reference). Returns \c false (nothing eaten) when the player is
        full. */
    bool eat(int points, double modifier);
    /*! Adds \a points of food and \a saturation even to a full bar (the saturation effect);
        nothing while frozen. */
    void feed(int points, double saturation);
    /*! Damage resets the regeneration timer. */
    void hurt() { setRegenerationTimer(0.0); }

    /*! Advances the timers by \a dt seconds for a player with \a health of \a maxHealth. Returns
        the health change: up to +1 for regeneration, a negative amount for starvation, 0
        otherwise. */
    double tick(double dt, double health, double maxHealth);

private:
    /*! Sets the food points to \a food and keeps the saturation within them. */
    void setFood(int food);
    /*! Sets the saturation to \a saturation, within 0 and the food points. */
    void setSaturation(double saturation);
    /*! Sets the exhaustion gathered towards the next point to \a exhaustion. */
    void setExhaustion(double exhaustion) { m_exhaustion = exhaustion; }
    /*! Returns the seconds since the last step of regeneration. */
    double regenerationTimer() const { return m_regenerationTimer; }
    /*! Sets the seconds since the last step of regeneration to \a seconds. */
    void setRegenerationTimer(double seconds) { m_regenerationTimer = seconds; }
    /*! Returns the seconds since the last hit of starvation. */
    double starvationTimer() const { return m_starvationTimer; }
    /*! Sets the seconds since the last hit of starvation to \a seconds. */
    void setStarvationTimer(double seconds) { m_starvationTimer = seconds; }
    /*! Advances the regeneration timer by \a dt seconds. Returns the health of a step due after
        \a interval seconds, at most \a missing: each health point exhausts \a perHealth, a step
        at most \a budget; 0 while none is due. */
    double regenerate(double dt, double interval, double missing, double budget, double perHealth);

    int m_food = maxFood;
    double m_saturation = spawnSaturation;
    double m_exhaustion = 0.0;
    double m_regenerationTimer = 0.0;
    double m_starvationTimer = 0.0;
    bool m_frozen = false;
};
