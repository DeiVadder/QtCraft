// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcItemDefinition.h"

/*! \brief Food points of one player (decision E-039).

    Activity adds exhaustion; every exhaustionPerFood of it costs one food point. With at
    least regenerationFood points the player heals one health point every
    regenerationInterval seconds, which exhausts as well; at zero food the player loses one
    health point every starvationInterval seconds, but never below starvationFloor. */
class QcHunger
{
public:
    /*! Food points of a full bar. */
    static constexpr int maxFood = QcItemDefinition::maxFood;
    /*! Food points at least for regeneration. */
    static constexpr int regenerationFood = 18;
    /*! Exhaustion that costs one food point. */
    static constexpr double exhaustionPerFood = 4.0;
    /*! Seconds between two health points of regeneration. */
    static constexpr double regenerationInterval = 4.0;
    /*! Exhaustion one health point of regeneration costs. */
    static constexpr double regenerationExhaustion = 2.0;
    /*! Seconds between two hits of starvation at zero food. */
    static constexpr double starvationInterval = 4.0;
    /*! Health one hit of starvation takes. */
    static constexpr double starvationDamage = 1.0;
    /*! Starvation never takes health below this. */
    static constexpr double starvationFloor = 1.0;

    /*! Exhaustion per activity (decision E-100). Without a saturation buffer these are about a
        quarter to a third of the classic values: walking empties a full bar in about two hours,
        sprinting in about eight minutes. \qcshared */
    static constexpr double walkPerMetre = 0.0025;
    static constexpr double sprintPerMetre = 0.03;
    static constexpr double swimPerMetre = 0.005;
    static constexpr double jump = 0.02;
    static constexpr double sprintJump = 0.08;
    static constexpr double mineBlock = 0.005;
    static constexpr double attack = 0.05;
    static constexpr double damageTaken = 0.05;

    /*! Returns the minutes of steady movement at \a speed metres per second with \a perMetre
        exhaustion that empty a full food bar. */
    static constexpr double minutesForFullBar(double perMetre, double speed)
    {
        return maxFood * exhaustionPerFood / (perMetre * speed) / 60.0;
    }

    /*! Returns the food points. */
    int food() const { return m_food; }
    /*! Returns the exhaustion gathered towards the next food point. */
    double exhaustion() const { return m_exhaustion; }
    /*! Takes \a food and \a exhaustion from a save, clamped to their ranges. */
    void restore(int food, double exhaustion);
    /*! Fills the bar and clears the exhaustion and timers. */
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
    /*! Adds the food \a points of an eaten item. Returns \c false (nothing eaten) when the
        player is full. */
    bool eat(int points);
    /*! Damage resets the regeneration timer. */
    void hurt() { m_regenerationTimer = 0.0; }

    /*! Advances the timers by \a dt seconds for a player with \a health of \a maxHealth. Returns
        the health change: +1 for regeneration, a negative amount for starvation, 0 otherwise. */
    double tick(double dt, double health, double maxHealth);

private:
    int m_food = maxFood;
    double m_exhaustion = 0.0;
    double m_regenerationTimer = 0.0;
    double m_starvationTimer = 0.0;
    bool m_frozen = false;
};
