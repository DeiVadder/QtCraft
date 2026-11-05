// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientEnchanting.h"

#include <utility>

bool QcClientEnchanting::sameState(const QcMsg::EnchantingState &a, const QcMsg::EnchantingState &b) const
{
    return a.item == b.item && a.lapis == b.lapis && a.shelves == b.shelves && a.offers == b.offers;
}

bool QcClientEnchanting::choose(int slot)
{
    if (slot < 0 || std::cmp_greater_equal(slot, state().offers.size())) {
        return false;
    }
    return sendWhileOpen(QcMsg::ChooseEnchantment{slot});
}
