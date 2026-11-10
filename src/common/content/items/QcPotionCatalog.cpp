// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/items/QcPotionCatalog.h"

void QcPotionCatalog::linkItem(QcItemId item, const QString &potion, QcPotionForm form)
{
    m_potionOfItem.insert(item, {potion, form});
    m_itemsOfPotion[potion][static_cast<std::size_t>(form)] = item;
}

std::optional<QcPotionCatalog::ItemPotion> QcPotionCatalog::potionOf(QcItemId item) const
{
    const auto it = m_potionOfItem.constFind(item);
    return it == m_potionOfItem.constEnd() ? std::nullopt : std::optional(*it);
}

std::optional<QcItemId> QcPotionCatalog::itemOf(const QString &potion, QcPotionForm form) const
{
    const auto it = m_itemsOfPotion.constFind(potion);
    if (it == m_itemsOfPotion.constEnd() || (*it)[static_cast<std::size_t>(form)] == qcNoItem) {
        return std::nullopt;
    }
    return (*it)[static_cast<std::size_t>(form)];
}
