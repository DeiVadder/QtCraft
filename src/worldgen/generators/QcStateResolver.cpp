// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/generators/QcStateResolver.h"

#include <utility>

using namespace Qt::StringLiterals;

QcStateResolver::QcStateResolver(const QcContentRegistry &content, QString structure, QString missing)
    : m_content(content)
    , m_structure(std::move(structure))
    , m_missing(std::move(missing))
{
}

QcBlockId QcStateResolver::state(const QString &name)
{
    const std::optional<QcBlockId> id = m_content.stateId(name);
    if (!id) {
        m_warnings.append(u"%1: unknown block '%2', %3"_s.arg(m_structure, name, m_missing));
        m_resolved = false;
    }
    return id.value_or(qcAirBlockId);
}

std::array<QcBlockId, 4> QcStateResolver::facing(const QString &name, const QString &others)
{
    static constexpr std::array<const char *, 4> sides{"north", "east", "south", "west"};
    std::array<QcBlockId, 4> states{};
    for (std::size_t i = 0; i < sides.size(); ++i) {
        states[i] = state(name + u"[facing="_s + QString::fromLatin1(sides[i]) + others + u']');
    }
    return states;
}
