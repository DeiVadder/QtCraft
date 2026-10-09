#pragma once

#include "QcItemDefinition.h"

// Food points of one player (decision E-039). Activity adds exhaustion; every
// `exhaustionPerFood` of it costs one food point. With at least `regenerationFood` points the player
// heals one health point every `regenerationInterval` seconds, which exhausts as well; at zero food
// the player loses one health point every `starvationInterval` seconds, but never below
// `starvationFloor`.
class QcHunger
{
public:
    static constexpr int maxFood = QcItemDefinition::maxFood;
    static constexpr int regenerationFood = 18;
    static constexpr double exhaustionPerFood = 4.0;
    static constexpr double regenerationInterval = 4.0;
    static constexpr double regenerationExhaustion = 3.0;
    static constexpr double starvationInterval = 4.0;
    static constexpr double starvationDamage = 1.0;
    static constexpr double starvationFloor = 1.0;

    // Exhaustion per activity.
    static constexpr double walkPerMetre = 0.01;
    static constexpr double sprintPerMetre = 0.1;
    static constexpr double swimPerMetre = 0.015;
    static constexpr double jump = 0.05;
    static constexpr double sprintJump = 0.2;
    static constexpr double mineBlock = 0.005;
    static constexpr double attack = 0.1;
    static constexpr double damageTaken = 0.1;

    int food() const { return m_food; }
    double exhaustion() const { return m_exhaustion; }
    // Values from a save; clamped to their ranges.
    void restore(int food, double exhaustion);
    void reset();

    void exhaust(double amount);
    // Exhaustion of one movement step: `distance` metres moved horizontally.
    static double movementExhaustion(double distance, bool sprinting, bool inWater, bool jumped);
    // Adds the food points of an eaten item; false (nothing eaten) when the player is full.
    bool eat(int points);
    // Damage resets the regeneration timer.
    void hurt() { m_regenerationTimer = 0.0; }

    // Advances the timers by `dt` seconds and returns the health change: +1 for regeneration,
    // a negative amount for starvation, 0 otherwise.
    double tick(double dt, double health, double maxHealth);

private:
    int m_food = maxFood;
    double m_exhaustion = 0.0;
    double m_regenerationTimer = 0.0;
    double m_starvationTimer = 0.0;
};
