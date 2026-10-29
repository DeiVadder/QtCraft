// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/explosions/QcBlastTable.h"

#include "common/content/registry/QcContentRegistry.h"

QcBlastTable::QcBlastTable(const QcContentRegistry &content)
    : m_content(content)
{
    const auto count = static_cast<std::size_t>(content.stateCount());
    m_entries.reserve(count);
    for (std::size_t id = 0; id < count; ++id) {
        const QcBlockDefinition &block = content.block(static_cast<QcBlockId>(id));
        Entry entry;
        entry.resistance = static_cast<float>(block.blast.effectiveResistance(block.hardness, block.liquid));
        entry.solid = block.solid;
        entry.explosive = block.blast.isExplosive();
        m_entries.push_back(entry);
    }
    // Air is nothing to a blast.
    if (!m_entries.empty()) {
        m_entries[qcAirBlockId] = {};
    }
}

const QcBlastTable::Entry &QcBlastTable::of(QcBlockId block) const
{
    static const Entry none;
    return static_cast<std::size_t>(block) < m_entries.size() ? m_entries[static_cast<std::size_t>(block)] : none;
}

const QcBlastDefinition &QcBlastTable::blast(QcBlockId block) const
{
    return m_content.block(block).blast;
}
