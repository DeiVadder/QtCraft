#include "clientcore/session/QcClientTrading.h"

#include <algorithm>

bool QcClientTrading::handle(const QcMessage &message)
{
    if (std::holds_alternative<QcMsg::CloseTrade>(message)) {
        if (isOpen()) {
            m_state = {};
            emit closed();
        }
        return true;
    }

    const auto *state = std::get_if<QcMsg::TradeState>(&message);
    if (!state) {
        return false;
    }
    if (m_closedLocally) {
        return true;
    }

    const bool opening = m_state.villager != state->villager;
    m_state = *state;
    if (opening) {
        emit opened();
    } else {
        emit changed();
    }
    return true;
}

bool QcClientTrading::trade(int index, bool repeat)
{
    if (!isOpen() || index < 0 || index >= m_state.offers.size() || !m_send) {
        return false;
    }
    m_send(QcMsg::Trade{m_state.villager, index, repeat});
    return true;
}

bool QcClientTrading::affordable(int index, const QList<QcItemStack> &inventory) const
{
    if (index < 0 || index >= m_state.offers.size()) {
        return false;
    }
    const QList<QcItemStack> &costs = m_state.offers[index].costs;
    const auto countOf = [](const QList<QcItemStack> &stacks, QcItemId item) {
        int count = 0;
        for (const QcItemStack &stack : stacks) {
            count += stack.item == item ? stack.count : 0;
        }
        return count;
    };
    return std::ranges::all_of(costs, [&](const QcItemStack &cost) { return countOf(inventory, cost.item) >= countOf(costs, cost.item); });
}

void QcClientTrading::close()
{
    if (!isOpen()) {
        return;
    }
    m_closedLocally = true;
    m_state = {};
    if (m_send) {
        m_send(QcMsg::CloseTrade{});
    }
    emit closed();
}
