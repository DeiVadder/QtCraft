// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/gameplay/QcHunger.h"

#include <algorithm>
#include <cmath>

namespace {

// Timers add up tick durations that are not exact in binary; this keeps whole intervals whole.
constexpr double timerSlack = 1e-9;

// Returns `value`, or 0 when it is not finite.
double finiteOrZero(double value)
{
    return std::isfinite(value) ? value : 0.0;
}

} // namespace

void QcHunger::restore(int food, double exhaustion, double saturation)
{
    setFood(std::clamp(food, 0, maxFood));
    setSaturation(finiteOrZero(saturation));
    setExhaustion(std::clamp(finiteOrZero(exhaustion), 0.0, exhaustionPerFood));
    setRegenerationTimer(0.0);
    setStarvationTimer(0.0);
}

void QcHunger::reset()
{
    restore(maxFood, 0.0, spawnSaturation);
}

void QcHunger::setFood(int food)
{
    m_food = food;
    setSaturation(saturation());
}

void QcHunger::setSaturation(double saturation)
{
    m_saturation = std::clamp(saturation, 0.0, static_cast<double>(food()));
}

void QcHunger::exhaust(double amount)
{
    if (frozen() || !std::isfinite(amount) || amount <= 0.0) {
        return;
    }
    setExhaustion(exhaustion() + amount);
    // Saturation goes first, then food (decision E-1001).
    while (exhaustion() >= exhaustionPerFood) {
        setExhaustion(exhaustion() - exhaustionPerFood);
        if (saturation() > 0.0) {
            setSaturation(saturation() - 1.0);
        } else {
            setFood(std::max(0, food() - 1));
        }
    }
}

double QcHunger::movementExhaustion(double distance, bool sprinting, bool inWater, bool jumped)
{
    const double perMetre = inWater ? swimPerMetre : (sprinting ? sprintPerMetre : walkPerMetre);
    const double moved = std::isfinite(distance) ? std::max(0.0, distance) : 0.0;
    return moved * perMetre + (jumped ? (sprinting ? sprintJump : jump) : 0.0);
}

bool QcHunger::eat(int points, double modifier)
{
    if (points <= 0 || food() >= maxFood) {
        return false;
    }
    feed(points, points * std::max(0.0, finiteOrZero(modifier)) * 2.0);
    return true;
}

void QcHunger::feed(int points, double saturation)
{
    if (frozen()) {
        return;
    }
    setFood(std::clamp(food() + std::max(points, 0), 0, maxFood));
    // The saturation never exceeds the food points, as in the reference.
    setSaturation(this->saturation() + std::max(0.0, finiteOrZero(saturation)));
}

double QcHunger::regenerate(double dt, double interval, double missing, double budget, double perHealth)
{
    setStarvationTimer(0.0);
    setRegenerationTimer(regenerationTimer() + dt);
    if (regenerationTimer() < interval - timerSlack) {
        return 0.0;
    }
    setRegenerationTimer(0.0);
    const double cost = std::min(perHealth, budget);
    exhaust(cost);
    return std::min(cost / perHealth, missing);
}

double QcHunger::tick(double dt, double health, double maxHealth)
{
    if (frozen()) {
        return 0.0;
    }

    // At a full bar the saturation heals quickly, a step costing at most what is left of it
    // (the reference's min(saturation, 6) for a sixth of a heart per point).
    const double missing = maxHealth - health;
    if (missing > 0.0 && food() >= maxFood && saturation() > 0.0) {
        return regenerate(dt, fastRegenerationInterval, missing, saturation(), fastRegenerationExhaustion);
    }
    if (missing > 0.0 && food() >= regenerationFood) {
        return regenerate(dt, regenerationInterval, missing, regenerationExhaustion, regenerationExhaustion);
    }

    setRegenerationTimer(0.0);
    if (food() > 0 || health <= starvationFloor) {
        setStarvationTimer(0.0);
        return 0.0;
    }

    setStarvationTimer(starvationTimer() + dt);
    if (starvationTimer() >= starvationInterval - timerSlack) {
        setStarvationTimer(0.0);
        return -std::min(starvationDamage, health - starvationFloor);
    }
    return 0.0;
}
