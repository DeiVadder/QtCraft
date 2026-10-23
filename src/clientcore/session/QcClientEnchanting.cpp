#include "clientcore/session/QcClientEnchanting.h"

#include <utility>

namespace {

bool sameState(const QcMsg::EnchantingState &a, const QcMsg::EnchantingState &b)
{
    return a.item == b.item && a.lapis == b.lapis && a.shelves == b.shelves && a.offers == b.offers;
}

} // namespace

bool QcClientEnchanting::handle(const QcMessage &message)
{
    if (std::holds_alternative<QcMsg::CloseEnchanting>(message)) {
        if (m_open) {
            m_open = false;
            m_state = {};
            emit closed();
        }
        return true;
    }
    if (const auto *state = std::get_if<QcMsg::EnchantingState>(&message)) {
        // A state still on its way after the player's own close belongs to no open screen.
        if (m_open && !sameState(*state, m_state)) {
            m_state = *state;
            emit changed();
        }
        return true;
    }
    const auto *open = std::get_if<QcMsg::OpenEnchanting>(&message);
    if (!open) {
        return false;
    }
    m_open = true;
    m_pos = open->pos;
    emit opened();
    return true;
}

bool QcClientEnchanting::choose(int slot)
{
    if (!m_open || !m_send || slot < 0 || std::cmp_greater_equal(slot, m_state.offers.size())) {
        return false;
    }
    m_send(QcMsg::ChooseEnchantment{slot});
    return true;
}

void QcClientEnchanting::close()
{
    if (!m_open) {
        return;
    }
    m_open = false;
    m_state = {};
    if (m_send) {
        m_send(QcMsg::CloseEnchanting{});
    }
    emit closed();
}
