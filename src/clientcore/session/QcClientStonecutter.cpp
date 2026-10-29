// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientStonecutter.h"

#include "common/content/registry/QcStonecutting.h"

bool QcClientStonecutter::handle(const QcMessage &message)
{
    if (std::holds_alternative<QcMsg::CloseStonecutter>(message)) {
        if (m_open) {
            m_open = false;
            m_input = {};
            m_recipe.clear();
            emit closed();
        }
        return true;
    }

    if (const auto *state = std::get_if<QcMsg::StonecutterState>(&message)) {
        // A state still on its way after the player's own close belongs to no open screen (the
        // server sends OpenStonecutter before the first state).
        if (m_open && (state->input != m_input || state->recipe != m_recipe)) {
            m_input = state->input;
            m_recipe = state->recipe;
            emit changed();
        }
        return true;
    }

    const auto *open = std::get_if<QcMsg::OpenStonecutter>(&message);
    if (!open) {
        return false;
    }
    m_open = true;
    m_pos = open->pos;
    emit opened();
    return true;
}

QList<QcItemStack> QcClientStonecutter::slotStacks(const QcContentRegistry &content) const
{
    return {m_input, {}, QcStonecutting::output(content, m_input, m_recipe)};
}

bool QcClientStonecutter::select(const QString &recipe)
{
    if (!m_open || !m_send) {
        return false;
    }
    m_send(QcMsg::SelectStonecutterRecipe{recipe});
    return true;
}

void QcClientStonecutter::close()
{
    if (!m_open) {
        return;
    }
    m_open = false;
    m_input = {};
    m_recipe.clear();
    if (m_send) {
        m_send(QcMsg::CloseStonecutter{});
    }
    emit closed();
}
