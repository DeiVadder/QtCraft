// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/showcase/QcShowcaseCreatureArea.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

struct Pen
{
    int inner = 3;
    int wall = 1;
    int height = 3;

    int outer() const { return inner + 2 * wall; }
};

Pen penFor(const QcMobDefinition &mob)
{
    Pen pen;
    pen.inner = std::max(3, static_cast<int>(std::ceil(mob.width)) + 2);
    // A hostile creature hits a player within attackRange of its centre and gets as close to the
    // glass as half its width: the wall is thicker than the rest of its reach (one without blows,
    // the witch, reaches nobody).
    const double reachBeyondGlass = std::max(0.0, mob.attackRange - mob.width / 2.0);
    pen.wall = mob.hostile && mob.attackDamage > 0.0 ? static_cast<int>(std::floor(reachBeyondGlass)) + 1 : 1;
    pen.height = mob.flying ? 5 : std::max(3, static_cast<int>(std::ceil(mob.height)) + 1);
    return pen;
}

// One column of a pen at `offset` (x and z) from its corner: a wall from the ground up unless the
// column is inside, a roof on top (`blocks`: of the walls, of the roof).
void buildPenColumn(QcStructure &structure, Pen pen, QcBlockPos corner, QcBlockPos offset, std::pair<QcBlockId, QcBlockId> blocks)
{
    const auto inPen = [&pen](int along) { return along >= pen.wall && along < pen.wall + pen.inner; };
    const int bottom = inPen(offset.x) && inPen(offset.z) ? pen.height + 1 : 1;
    for (int y = bottom; y <= pen.height + 1; ++y) {
        structure.set(corner.x + offset.x, y, corner.z + offset.z, y == pen.height + 1 ? blocks.second : blocks.first);
    }
}

// Walls from the ground up to the pen's height and a roof above, from the local corner on
// (`blocks`: of the walls, of the roof).
void buildPen(QcStructure &structure, Pen pen, QcBlockPos corner, std::pair<QcBlockId, QcBlockId> blocks)
{
    for (int x = 0; x < pen.outer(); ++x) {
        for (int z = 0; z < pen.outer(); ++z) {
            buildPenColumn(structure, pen, corner, {x, 0, z}, blocks);
        }
    }
}

// Fills the inside of a pen with water up to its roof, for water creatures (decision E-502) and
// divers.
void floodPen(QcStructure &structure, Pen pen, QcBlockPos corner, QcBlockId water)
{
    for (int x = pen.wall; x < pen.wall + pen.inner; ++x) {
        for (int z = pen.wall; z < pen.wall + pen.inner; ++z) {
            for (int y = 1; y <= pen.height; ++y) {
                structure.set(corner.x + x, y, corner.z + z, water);
            }
        }
    }
}

} // namespace

namespace QcShowcaseCreatureArea {

QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor)
{
    const QList<QcMobDefinition> &mobs = context.content.mobs();
    int width = 0;
    int depth = 1;
    int height = 1;
    for (const QcMobDefinition &mob : mobs) {
        const Pen pen = penFor(mob);
        width += pen.outer() + 1;
        depth = std::max(depth, pen.outer());
        height = std::max(height, pen.height + 4);
    }

    auto structure = std::make_shared<QcStructure>(u"showcase/creatures"_s, std::max(1, width), height, depth);
    int x = 0;
    for (const QcMobDefinition &mob : mobs) {
        const Pen pen = penFor(mob);
        // Pens line up along the south edge, the side people come from.
        const int z = depth - pen.outer();
        // The showcase stands at noon: creatures that burn in daylight get a roof of the podium's
        // opaque block for shade (decision E-426).
        buildPen(*structure, pen, {x, 0, z}, {context.enclosure, mob.burnsInDaylight ? context.podium : context.enclosure});
        // Water creatures and divers (the drowned, decision E-838) live in water.
        if (mob.aquatic || mob.abilities.dives) {
            floodPen(*structure, pen, {x, 0, z}, context.content.blockId(u"qtcraft:water"_s).value_or(qcAirBlockId));
        }

        const double centreX = x + pen.wall + pen.inner / 2.0;
        const double centreZ = z + pen.wall + pen.inner / 2.0;
        // Confined: a creature that teleports or takes blocks stays in its pen and changes nothing
        // (decisions E-460, E-510).
        structure->mobs.append({mob.name, {centreX, 1.0, centreZ}, true});
        structure->labels.append({{centreX, pen.height + 2.5, static_cast<double>(depth) - 0.5}, mob.translationKey, {}, false});
        x += pen.outer() + 1;
    }

    const QcBlockPos origin{anchor.x, anchor.y, anchor.z - depth + 1};
    return {u"creatures"_s, u"showcase.area.creatures"_s, std::move(structure), origin, {}, {}};
}

} // namespace QcShowcaseCreatureArea
