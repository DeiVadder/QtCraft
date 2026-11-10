// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcPortalSparkles.h"

#include "clientcore/session/QcCampfireSmoke.h"
#include "common/blockrules/QcPortalShapes.h"

#include <cmath>
#include <numbers>

QcPortalSparkles::QcPortalSparkles()
    : m_blocks({radius, rescanTicks, maxBlocks},
               [](const QcContentRegistry &content, QcBlockId id) { return QcPortalShapes::portalOfBlock(content, id) != nullptr; })
{
}

QList<QcEffectPuff> QcPortalSparkles::specksOf(QcBlockPos pos, double seconds)
{
    QList<QcEffectPuff> specks;
    for (int index = 0; index < specksPerBlock; ++index) {
        // Each speck starts somewhere in the block and swirls outwards on its own circle.
        const double time = seconds / speckSeconds + static_cast<double>(index) / specksPerBlock;
        const auto round = static_cast<qint64>(std::floor(time));
        const double age = time - static_cast<double>(round);
        const double start = QcCampfireSmoke::unitOf(pos, index, round, 7);
        const double angle = 2.0 * std::numbers::pi * (start + age * 0.75);
        const double reach = 0.15 + 0.55 * age;
        const QcVec3 centre{pos.x + 0.5, pos.y + 0.2 + QcCampfireSmoke::unitOf(pos, index, round, 9) * 0.6, pos.z + 0.5};
        const QcVec3 offset{std::cos(angle) * reach, age * 0.4, std::sin(angle) * reach};
        specks.append({centre + offset, 0.09 * std::sin(age * std::numbers::pi)});
    }
    return specks;
}
