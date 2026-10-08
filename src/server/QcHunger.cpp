#include "QcHunger.h"

#include <algorithm>
#include <cmath>

namespace {

// Timers add up tick durations that are not exact in binary; this keeps whole intervals whole.
constexpr double timerSlack = 1e-9;

} // namespace

void QcHunger::restore(int food, double exhaustion)
{
    m_food = std::clamp(food, 0, maxFood);
    m_exhaustion = std::isfinite(exhaustion) ? std::clamp(exhaustion, 0.0, exhaustionPerFood) : 0.0;
    m_regenerationTimer = 0.0;
    m_starvationTimer = 0.0;
}

void QcHunger::reset()
{
    restore(maxFood, 0.0);
}

void QcHunger::exhaust(double amount)
{
    if (!std::isfinite(amount) || amount <= 0.0) {
        return;
    }
    m_exhaustion += amount;
    while (m_exhaustion >= exhaustionPerFood) {
        m_exhaustion -= exhaustionPerFood;
        m_food = std::max(0, m_food - 1);
    }
}

double QcHunger::movementExhaustion(double distance, bool sprinting, bool inWater, bool jumped)
{
    const double perMetre = inWater ? swimPerMetre : (sprinting ? sprintPerMetre : walkPerMetre);
    const double moved = std::isfinite(distance) ? std::max(0.0, distance) : 0.0;
    return moved * perMetre + (jumped ? (sprinting ? sprintJump : jump) : 0.0);
}

bool QcHunger::eat(int points)
{
    if (points <= 0 || m_food >= maxFood) {
        return false;
    }
    m_food = std::min(maxFood, m_food + points);
    return true;
}

double QcHunger::tick(double dt, double health, double maxHealth)
{
    if (m_food >= regenerationFood && health < maxHealth) {
        m_starvationTimer = 0.0;
        m_regenerationTimer += dt;
        if (m_regenerationTimer >= regenerationInterval - timerSlack) {
            m_regenerationTimer = 0.0;
            exhaust(regenerationExhaustion);
            return std::min(1.0, maxHealth - health);
        }
        return 0.0;
    }
    m_regenerationTimer = 0.0;
    if (m_food > 0 || health <= starvationFloor) {
        m_starvationTimer = 0.0;
        return 0.0;
    }
    m_starvationTimer += dt;
    if (m_starvationTimer >= starvationInterval - timerSlack) {
        m_starvationTimer = 0.0;
        return -std::min(starvationDamage, health - starvationFloor);
    }
    return 0.0;
}
