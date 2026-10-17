#include "worldgen/showcase/QcShowcaseCreatureArea.h"

#include <algorithm>
#include <cmath>

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
    pen.inner = std::max(3, int(std::ceil(mob.width)) + 2);
    // A hostile creature hits a player within attackRange of its centre and gets as close to the
    // glass as half its width: the wall is thicker than the rest of its reach.
    const double reachBeyondGlass = std::max(0.0, mob.attackRange - mob.width / 2.0);
    pen.wall = mob.hostile ? int(std::floor(reachBeyondGlass)) + 1 : 1;
    pen.height = mob.flying ? 5 : std::max(3, int(std::ceil(mob.height)) + 1);
    return pen;
}

// One column of a pen at `offset` (x and z) from its corner: a wall from the ground up unless the
// column is inside, a roof on top.
void buildPenColumn(QcStructure &structure, Pen pen, QcBlockPos corner, QcBlockPos offset, QcBlockId glass)
{
    const auto inPen = [&pen](int along) { return along >= pen.wall && along < pen.wall + pen.inner; };
    const int bottom = inPen(offset.x) && inPen(offset.z) ? pen.height + 1 : 1;
    for (int y = bottom; y <= pen.height + 1; ++y) {
        structure.set(corner.x + offset.x, y, corner.z + offset.z, glass);
    }
}

// Walls from the ground up to the pen's height and a roof above, from the local corner on.
void buildPen(QcStructure &structure, Pen pen, QcBlockPos corner, QcBlockId glass)
{
    for (int x = 0; x < pen.outer(); ++x) {
        for (int z = 0; z < pen.outer(); ++z) {
            buildPenColumn(structure, pen, corner, {x, 0, z}, glass);
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
        buildPen(*structure, pen, {x, 0, z}, context.enclosure);

        const double centreX = x + pen.wall + pen.inner / 2.0;
        const double centreZ = z + pen.wall + pen.inner / 2.0;
        structure->mobs.append({mob.name, {centreX, 1.0, centreZ}});
        structure->labels.append({{centreX, pen.height + 2.5, double(depth) - 0.5}, mob.translationKey, {}, false});
        x += pen.outer() + 1;
    }

    const QcBlockPos origin{anchor.x, anchor.y, anchor.z - depth + 1};
    return {u"creatures"_s, u"showcase.area.creatures"_s, std::move(structure), origin, {}};
}

} // namespace QcShowcaseCreatureArea
