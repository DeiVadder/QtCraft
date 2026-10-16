#include "QcClientVitals.h"

QcClientVitals::Change QcClientVitals::apply(const QcMessage &message)
{
    if (const auto *health = std::get_if<QcMsg::PlayerHealth>(&message)) {
        m_health = *health;
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
    return Change::None;
}
