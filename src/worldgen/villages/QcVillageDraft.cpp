#include "worldgen/villages/QcVillageDraft.h"

#include "common/content/parser/QcVillageLayoutParser.h"
#include "worldgen/surface/QcSurfaceBlend.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcBoardwalkPlanner.h"
#include "worldgen/villages/QcVillageRoads.h"

#include <algorithm>

namespace {

// Posts stand under a bridge's deck on a grid of this many blocks (decision E-841).
constexpr int bridgePostSpacing = 3;

} // namespace

int QcVillageDraft::turnsFacing(int fx, int fz)
{
    if (fz > 0) {
        return 0;
    }
    if (fx < 0) {
        return 1;
    }
    return fz < 0 ? 2 : 3;
}

QcVillageDraft::Rect QcVillageDraft::brush(std::pair<int, int> cell, int dx, int dz, bool square)
{
    const auto [x, z] = cell;
    if (square) {
        return {x - roadHalfWidth, z - roadHalfWidth, x + roadHalfWidth, z + roadHalfWidth};
    }
    // Across an axis run only.
    return {x - roadHalfWidth * std::abs(dz), z - roadHalfWidth * std::abs(dx), x + roadHalfWidth * std::abs(dz), z + roadHalfWidth * std::abs(dx)};
}

double QcVillageDraft::random()
{
    return qcPositionRandom(seeds.layout, region.x, nextRandom++, region.z);
}

double QcVillageDraft::variantRandom()
{
    return qcPositionRandom(seeds.variant, region.x, nextVariant++, region.z);
}

double QcVillageDraft::cropRandom()
{
    return qcPositionRandom(seeds.crop, region.x, nextCrop++, region.z);
}

bool QcVillageDraft::insideRadius(Rect rect) const
{
    const int reach = region.radius;
    return rect.x0 >= centreX - reach && rect.x1 <= centreX + reach && rect.z0 >= centreZ - reach && rect.z1 <= centreZ + reach;
}

bool QcVillageDraft::touchesRoad(Rect rect, int gap) const
{
    for (int z = rect.z0 - gap; z <= rect.z1 + gap; ++z) {
        for (int x = rect.x0 - gap; x <= rect.x1 + gap; ++x) {
            if (roadCells.contains(cellKey(x, z))) {
                return true;
            }
        }
    }
    return false;
}

void QcVillageDraft::markRoad(Rect rect)
{
    for (int z = rect.z0; z <= rect.z1; ++z) {
        for (int x = rect.x0; x <= rect.x1; ++x) {
            roadCells.insert(cellKey(x, z));
        }
    }
    roads.append(rect);
}

void QcVillageDraft::addRoadCell(const QcTerrainGenerator &terrain, std::pair<int, int> cell, bool branch, std::optional<RoadLevel> level)
{
    const auto [x, z] = cell;
    const bool underPiece = std::ranges::any_of(pieces, [&](Rect piece) { return piece.intersects({x, z, x, z}, 0); });
    if (underPiece || roadCells.contains(cellKey(x, z))) {
        return;
    }

    roadCells.insert(cellKey(x, z));
    const QcTerrainColumn column = terrain.column(x, z);
    if (!style->overWater && style->bridge.deck != qcAirBlockId && column.height < terrain.seaLevel() && level && level->bridge) {
        // A bridge over a river (decision E-841): a deck one block above the water, posts on a grid.
        const bool post = x % bridgePostSpacing == 0 && z % bridgePostSpacing == 0;
        plan.bridges.append({{x, level->y, z}, style->bridge.deck, post ? style->bridge.post : qcAirBlockId});
    } else if (!style->overWater) {
        const int ground = groundTop(terrain, x, z);
        const int y = level && std::abs(level->y - ground) <= 1 ? level->y : ground;
        (branch ? plan.branchRoads : plan.roads).append({x, y, z});
    } else {
        const QcBlockId block = branch ? plan.branchPathBlock : plan.pathBlock;
        plan.decks.append(QcBoardwalkPlanner::deck(terrain, style->stilts, {x, z}, block));
    }
}

void QcVillageDraft::start()
{
    plan.id = QcVillagePlan::regionId(region.x, region.z);
    plan.style = style->name;

    // Indices below the variant sequence's (which starts at 0).
    const QcVillageStyle::Road &road = QcVillageRoads::choose(style->roads, qcPositionRandom(seeds.variant, region.x, -1, region.z));
    const QcVillageStyle::Road &branch = QcVillageRoads::choose(style->branchRoads, qcPositionRandom(seeds.variant, region.x, -2, region.z));
    plan.pathBlock = road.block;
    plan.branchPathBlock = branch.block;
    pathStep = road.step;
    branchStep = branch.step;

    plan.foundation = style->foundation;
    if (style->overWater) {
        plan.stiltPost = style->stilts.post;
        plan.stiltSpacing = style->stilts.spacing;
    }
    counts.fill(0, style->pieces.size());
}

void QcVillageDraft::setLayout(Layout layout)
{
    shape = layout.shape;
    mainDirection = layout.mainDirection;
    winding = layout.winding;
    plan.winding = layout.winding;
    setLayoutKind(layout.shape->kind);
}

void QcVillageDraft::setLayoutKind(QcVillageLayout::Kind kind)
{
    plan.layout = QcVillageLayoutParser::kindName(kind);
}

int QcVillageDraft::nextArm() const
{
    return plan.runs.isEmpty() ? 0 : plan.runs.back().arm + 1;
}

void QcVillageDraft::addRun(const Road &run, int arm)
{
    const int endX = run.x + run.dx * (run.length - 1);
    const int endZ = run.z + run.dz * (run.length - 1);
    const Rect first = brush({run.x, run.z}, run.dx, run.dz, true);
    const Rect last = brush({endX, endZ}, run.dx, run.dz, true);
    roads.append({std::min(first.x0, last.x0), std::min(first.z0, last.z0), std::max(first.x1, last.x1), std::max(first.z1, last.z1)});
    plan.runs.append({run.x, run.z, run.dx, run.dz, run.length, arm, run.branch});
    plan.roadLength += run.length;
}

void QcVillageDraft::addStairs(const Stairs &laid)
{
    plan.decks.append(laid.flight);
    markRoad(laid.extent);
    stairs.append(laid.extent);
    plan.wetland.dry.append({laid.dry.x0, laid.dry.z0, laid.dry.x1, laid.dry.z1});
}

void QcVillageDraft::addJetty(const QList<QcVillagePlan::Deck> &decks, Rect deck)
{
    plan.decks.append(decks);
    markRoad(deck);
}

const QcStructurePlacement &QcVillageDraft::addPiece(QcStructurePlacement piece, Rect rect, const QList<QcVillageSpot> &spots,
                                                     const QList<QcBlockPos> &crops)
{
    plan.pieces.append(std::move(piece));
    pieces.append(rect);
    const QcStructurePlacement &placed = plan.pieces.back();
    for (const QcVillageSpot &spot : spots) {
        QList<QcVillagePlan::Spot> &list = spot.kind.isEmpty() ? plan.beds : plan.workstations;
        list.append(QcVillageSpot{placed.toWorld(spot.at), spot.kind});
    }
    for (const QcStructure::Mob &mob : placed.structure().mobs) {
        plan.mobs.append({mob.mob, placed.toWorld(mob.at), mob.confined, mob.noAi});
    }
    for (const QcBlockPos &cell : crops) {
        plan.fields.insert(placed.toWorld(cell));
    }
    return placed;
}

void QcVillageDraft::addThresholds(const QList<QcBlockPos> &steps)
{
    for (const QcBlockPos &at : steps) {
        if (!roadCells.contains(cellKey(at.x, at.z))) {
            roadCells.insert(cellKey(at.x, at.z));
            plan.thresholds.append(at);
            plan.branchRoads.append(at);
        }
    }
}

bool QcVillageDraft::nearPiece(Rect rect) const
{
    for (qsizetype i = 0; i < pieces.size(); ++i) {
        const int gap = i == 0 ? std::max(pieceGap, plan.centreBlend) : pieceGap;
        if (rect.intersects(pieces[i], gap)) {
            return true;
        }
    }
    return false;
}

int QcVillageDraft::groundTop(const QcTerrainGenerator &terrain, int x, int z) const
{
    const int ground = terrain.column(x, z).height - 1;
    if (plan.centreBlend <= 0 || plan.pieces.isEmpty()) {
        return ground;
    }
    const QcStructurePlacement &centre = plan.pieces.front();
    if (QcSurfaceBlend::distanceTo(centre, x, z) == 0.0) {
        return centre.origin().y + centre.structure().groundLayer;
    }
    return QcSurfaceBlend(terrain).topAt(centre, plan.centreBlend, x, z, ground).value_or(ground);
}

void QcVillageDraft::coverArea()
{
    plan.minX = plan.maxX = centreX;
    plan.minZ = plan.maxZ = centreZ;
    QList<Rect> covered = pieces + roads;
    // The centre's blended ground is the village's too: no tree grows on ground that moves.
    if (plan.centreBlend > 0 && !pieces.isEmpty()) {
        const Rect centre = pieces.front();
        covered.append({centre.x0 - plan.centreBlend, centre.z0 - plan.centreBlend, centre.x1 + plan.centreBlend, centre.z1 + plan.centreBlend});
    }
    for (const Rect rect : std::as_const(covered)) {
        plan.minX = std::min(plan.minX, rect.x0);
        plan.minZ = std::min(plan.minZ, rect.z0);
        plan.maxX = std::max(plan.maxX, rect.x1);
        plan.maxZ = std::max(plan.maxZ, rect.z1);
    }
    if (style->overWater && style->wetland.bottom != qcAirBlockId) {
        spreadWetland();
    }
}

void QcVillageDraft::spreadWetland()
{
    // The wetland spreads from the pieces and boardwalks, not from the stairs down to land.
    QcVillagePlan::Wetland &wetland = plan.wetland;
    wetland.settings = style->wetland;
    const QList<Rect> covered = pieces + roads;
    for (const Rect rect : covered) {
        if (!stairs.contains(rect)) {
            wetland.wet.append({rect.x0, rect.z0, rect.x1, rect.z1});
        }
    }

    // Its columns are part of the village: no trees or plants grow there (decision E-403).
    // The village stays inside its region, so its chunks need no other plan.
    const int reach = wetland.settings.reach();
    const int regionX0 = region.x * region.spacing;
    const int regionZ0 = region.z * region.spacing;
    plan.minX = std::max(regionX0, plan.minX - reach);
    plan.minZ = std::max(regionZ0, plan.minZ - reach);
    plan.maxX = std::min(regionX0 + region.spacing - 1, plan.maxX + reach);
    plan.maxZ = std::min(regionZ0 + region.spacing - 1, plan.maxZ + reach);
}
