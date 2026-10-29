// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientAnvil.h"

#include <utility>

namespace {

bool sameState(const QcMsg::AnvilState &a, const QcMsg::AnvilState &b)
{
    return a.left == b.left && a.right == b.right && a.name == b.name && a.output == b.output && a.levelCost == b.levelCost;
}

} // namespace

bool QcClientAnvil::handle(const QcMessage &message)
{
    if (std::holds_alternative<QcMsg::CloseAnvil>(message)) {
        if (m_open) {
            m_open = false;
            m_state = {};
            emit closed();
        }
        return true;
    }
    if (const auto *state = std::get_if<QcMsg::AnvilState>(&message)) {
        // A state still on its way after the player's own close belongs to no open screen.
        if (m_open && !sameState(*state, m_state)) {
            m_state = *state;
            emit changed();
        }
        return true;
    }
    const auto *open = std::get_if<QcMsg::OpenAnvil>(&message);
    if (!open) {
        return false;
    }
    m_open = true;
    m_pos = open->pos;
    emit opened();
    return true;
}

bool QcClientAnvil::rename(const QString &name)
{
    if (!m_open || !m_send) {
        return false;
    }
    m_send(QcMsg::RenameAnvil{name.left(QcMsg::RenameAnvil::maxText)});
    return true;
}

bool QcClientAnvil::forge()
{
    if (!m_open || !m_send || m_state.output.isEmpty()) {
        return false;
    }
    m_send(QcMsg::ForgeAnvil{m_state.levelCost, m_state.output});
    return true;
}

void QcClientAnvil::close()
{
    if (!m_open) {
        return;
    }
    m_open = false;
    m_state = {};
    if (m_send) {
        m_send(QcMsg::CloseAnvil{});
    }
    emit closed();
}
