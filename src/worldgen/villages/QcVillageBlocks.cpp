// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/villages/QcVillageBlocks.h"

using namespace Qt::StringLiterals;

QcVillageBlocks::QcVillageBlocks(const QcContentRegistry &content)
    : m_bedHead(static_cast<std::size_t>(content.stateCount()))
    , m_bell(static_cast<std::size_t>(content.stateCount()))
    , m_kind(static_cast<std::size_t>(content.stateCount()))
{
    const QcVillageSettings &settings = content.villageCatalog().settings();
    for (auto it = settings.workstations.constBegin(); it != settings.workstations.constEnd(); ++it) {
        if (!content.blockId(it.key())) {
            m_warnings.append(u"village settings: unknown workstation block '%1'"_s.arg(it.key()));
        }
    }

    for (std::size_t id = 0; id < m_kind.size(); ++id) {
        const QcBlockDefinition &block = content.block(static_cast<QcBlockId>(id));
        m_bedHead[id] = block.bed && content.property(static_cast<QcBlockId>(id), u"part"_s) == u"head"_s;
        m_bell[id] = !settings.bell.isEmpty() && block.name == settings.bell;
        m_kind[id] = settings.workstations.value(block.name);
    }
}
