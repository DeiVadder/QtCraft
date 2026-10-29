// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/weather/QcDropPool.h"

#include <algorithm>

std::shared_ptr<const QList<QcPrecipitationDrop>> QcDropPool::fill(const QcPrecipitationField &field, float strength, const QcVec3 &origin)
{
    const auto slot = std::ranges::find_if(m_lists, [](const auto &list) { return !list || list.use_count() == 1; });
    std::shared_ptr<QList<QcPrecipitationDrop>> drops = slot != m_lists.end() ? *slot : nullptr;
    if (!drops) {
        drops = std::make_shared<QList<QcPrecipitationDrop>>();
        if (slot != m_lists.end()) {
            *slot = drops;
        }
    }

    // Reuse precondition: nobody but the pool (the slot and `drops`) holds the list being filled.
    Q_ASSERT(drops.use_count() == (slot != m_lists.end() ? 2 : 1));

    // Cleared with its capacity kept: a reused list allocates nothing.
    drops->clear();
    drops->reserve(field.particles().size());
    for (const QcPrecipitationField::Particle &particle : field.particles()) {
        drops->append(QcPrecipitationField::dropOf(particle, strength, origin));
    }
    return drops;
}
