#include "worldgen/villages/QcVillagePlanner.h"

#include "common/content/parser/QcVillageLayoutParser.h"

#include <algorithm>

// The street plans of the village planner (decision E-408): which layout a village takes and the
// roads of each kind, from the centre or a ring around it, with side roads off them.

namespace {

using Kind = QcVillageLayout::Kind;

// Side roads with one candidate spot (`branchEvery` 0) turn off roads at least this long.
constexpr int branchMinimum = 28;
// Side roads with spots every few blocks stop this far before the end of their road.
constexpr int branchEndMargin = 8;
// A road out from the ring is at least this long, or it is left out.
constexpr int exitMinimum = 10;
// Tree arms try this many random directions, the first half keeping a right angle to the others.
constexpr int treeAttempts = 48;

int turnDistance(int a, int b)
{
    const int d = std::abs(a - b) % 8;
    return std::min(d, 8 - d);
}

} // namespace

int QcVillagePlanner::drawCount(Layout &layout, QcVillageRange<int> range) const
{
    return range.min + std::min(range.max - range.min, int(random(layout) * double(range.max - range.min + 1)));
}

double QcVillagePlanner::drawShare(Layout &layout, QcVillageRange<double> range) const
{
    return range.min + (range.max - range.min) * random(layout);
}

void QcVillagePlanner::chooseLayout(Layout &layout) const
{
    const QList<QcVillageLayout> &layouts = layout.style->layouts;
    int total = 0;
    for (const QcVillageLayout &entry : layouts) {
        total += entry.weight;
    }
    int pick = int(random(layout) * total);
    layout.shape = &layouts.back();
    for (const QcVillageLayout &entry : layouts) {
        if (pick < entry.weight) {
            layout.shape = &entry;
            break;
        }
        pick -= entry.weight;
    }

    // A street may run along a diagonal; a tree with diagonals has its main arm on one, so it is
    // never just a cross. The other kinds keep to the axes.
    const QcVillageLayout &shape = *layout.shape;
    const double draw = random(layout);
    if (shape.diagonals && shape.kind == Kind::Street) {
        layout.mainDirection = int(draw * 8.0);
    } else if (shape.diagonals && shape.kind == Kind::Tree) {
        layout.mainDirection = 2 * int(draw * 4.0) + 1;
    } else {
        layout.mainDirection = 2 * int(draw * 4.0);
    }
    layout.winding = shape.diagonals && random(layout) < shape.winding;
    layout.plan.layout = QcVillageLayoutParser::kindName(shape.kind);
    layout.plan.winding = layout.winding;
}

QcVillagePlanner::Arm QcVillagePlanner::fromCentre(Layout &layout, int direction) const
{
    Arm arm;
    arm.x = layout.centreX;
    arm.z = layout.centreZ;
    arm.direction = direction % 8;
    arm.length = int(m_settings.radius * drawShare(layout, layout.shape->armLength));
    arm.firstSlot = clearOfCentre(layout, arm.direction);
    arm.winding = layout.winding;
    return arm;
}

void QcVillagePlanner::layRoads(Layout &layout, QList<Road> &roads) const
{
    const int main = layout.mainDirection;
    QList<Arm> arms;
    QList<QList<Road>> trunks;
    switch (layout.shape->kind) {
    case Kind::Street:
        arms = {fromCentre(layout, main), fromCentre(layout, main + 4)};
        break;
    case Kind::Tee:
        arms = {fromCentre(layout, main), fromCentre(layout, main + 2), fromCentre(layout, main + 6)};
        break;
    case Kind::Ell:
        arms = {fromCentre(layout, main), fromCentre(layout, main + (random(layout) < 0.5 ? 2 : 6))};
        break;
    case Kind::Tree:
        arms = treeArms(layout);
        break;
    case Kind::Plaza:
        if (layPlaza(layout, roads, trunks)) {
            break;
        }
        // A ring cut by water or a steep step: the village is a cross instead.
        layout.plan.layout = QcVillageLayoutParser::kindName(Kind::Cross);
        [[fallthrough]];
    case Kind::Cross:
        arms = {fromCentre(layout, main), fromCentre(layout, main + 2), fromCentre(layout, main + 4), fromCentre(layout, main + 6)};
        break;
    }
    layArms(layout, arms, roads, &trunks);
    layBranches(layout, trunks, roads);
}

void QcVillagePlanner::layArms(Layout &layout, const QList<Arm> &arms, QList<Road> &roads, QList<QList<Road>> *trunks) const
{
    for (const Arm &arm : arms) {
        const QList<Road> runs = layArm(layout, arm);
        if (runs.isEmpty()) {
            continue;
        }
        roads.append(runs);
        if (trunks) {
            trunks->append(runs);
        }
    }
}

bool QcVillagePlanner::layPlaza(Layout &layout, QList<Road> &roads, QList<QList<Road>> &trunks) const
{
    // The ring's inner edge keeps two blocks of grass to the centre piece on every side.
    int clear = 0;
    for (int direction = 0; direction < 8; direction += 2) {
        clear = std::max(clear, clearOfCentre(layout, direction));
    }
    const int ring = std::max(drawCount(layout, layout.shape->ring), clear + 3);

    // A side road from the centre to the ring, without pieces, towards the main direction or else
    // the first side it reaches the ring on in full; without one the plaza is left out.
    Arm spoke = fromCentre(layout, layout.mainDirection);
    spoke.length = ring;
    spoke.branch = true;
    spoke.winding = false;
    spoke.closed = true;
    spoke.sides = 0;
    int turn = 0;
    while (turn < 8 && walkArm(layout, spoke).isEmpty()) {
        turn += 2;
        spoke.direction = (layout.mainDirection + turn) % 8;
        spoke.firstSlot = clearOfCentre(layout, spoke.direction);
    }
    if (turn == 8) {
        return false;
    }

    Arm loop;
    loop.x = layout.centreX - ring;
    loop.z = layout.centreZ - ring;
    loop.length = 8 * ring;
    loop.bends = {{2 * ring + 1, 2}, {4 * ring + 1, 2}, {6 * ring + 1, 2}};
    loop.closed = true;
    // Clockwise, the left side faces out: the square inside stays open.
    loop.sides = 1;
    const QList<Road> runs = layArm(layout, loop);
    if (runs.isEmpty()) {
        return false;
    }
    roads.append(runs);

    QList<int> sides{layout.mainDirection, layout.mainDirection + 2, layout.mainDirection + 4, layout.mainDirection + 6};
    const int exits = drawCount(layout, layout.shape->exits);
    QList<Arm> arms;
    for (int n = 0; n < exits && !sides.isEmpty(); ++n) {
        const int direction = sides.takeAt(std::min(sides.size() - 1, qsizetype(random(layout) * double(sides.size())))) % 8;
        const auto [dx, dz] = directions[direction];
        // Off the middle of its side, so the exits and the spoke never line up into a cross.
        const int shift = exitShift(layout, ring);
        Arm exit;
        exit.x = layout.centreX + dx * ring + dz * shift;
        exit.z = layout.centreZ + dz * ring + dx * shift;
        exit.direction = direction;
        exit.length = int(m_settings.radius * drawShare(layout, layout.shape->armLength)) - ring;
        exit.firstSlot = roadHalfWidth + 1;
        exit.winding = layout.winding;
        if (exit.length >= exitMinimum) {
            arms.append(exit);
        }
    }
    layArms(layout, arms, roads, &trunks);
    layArms(layout, {spoke}, roads, nullptr);
    return true;
}

int QcVillagePlanner::exitShift(Layout &layout, int ring) const
{
    // Two to ring / 2 blocks to either side; the corners of the ring stay clear.
    const int most = std::max(2, std::min(ring / 2, ring - roadHalfWidth - 2));
    const int shift = 2 + int(random(layout) * double(most - 1));
    return random(layout) < 0.5 ? -shift : shift;
}

QList<QcVillagePlanner::Arm> QcVillagePlanner::treeArms(Layout &layout) const
{
    QList<Arm> arms;
    const QList<int> headings = treeDirections(layout);
    for (const int direction : headings) {
        Arm arm = fromCentre(layout, direction);
        const int bends = drawCount(layout, layout.shape->bends);
        for (int k = 0; k < bends; ++k) {
            const int at = arm.firstSlot + (arm.length - arm.firstSlot) * (k + 1) / (bends + 1);
            const int turn = (random(layout) < 0.5 ? -1 : 1) * (layout.shape->diagonals ? 1 : 2);
            arm.bends.append({at, turn});
        }
        arms.append(arm);
    }
    return arms;
}

QList<int> QcVillagePlanner::treeDirections(Layout &layout) const
{
    const bool diagonals = layout.shape->diagonals;
    const int count = std::min(drawCount(layout, layout.shape->arms), diagonals ? 8 : 4);
    QList<int> chosen{layout.mainDirection};
    for (int attempt = 0; attempt < treeAttempts && chosen.size() < count; ++attempt) {
        const int direction = diagonals ? int(random(layout) * 8.0) : 2 * int(random(layout) * 4.0);
        const int apart = attempt < treeAttempts / 2 ? 2 : 1;
        if (std::ranges::all_of(chosen, [&](int other) { return turnDistance(direction, other) >= apart; })) {
            chosen.append(direction);
        }
    }
    return chosen;
}

QList<std::pair<int, int>> QcVillagePlanner::branchSpots(Layout &layout, const QList<Road> &trunk) const
{
    const QcVillageLayout &shape = *layout.shape;
    const int length = trunk.back().armOffset + trunk.back().length;
    QList<std::pair<int, int>> spots;
    if (shape.branchEvery == 0) {
        if (length >= branchMinimum && random(layout) < shape.branchChance) {
            const int at = int(length * (0.5 + 0.2 * random(layout)));
            spots.append({at, random(layout) < 0.5 ? -1 : 1});
        }
        return spots;
    }

    // Every few blocks, the sides taking turns.
    int side = random(layout) < 0.5 ? -1 : 1;
    for (int at = trunk.front().firstSlot + shape.branchEvery / 2; at < length - branchEndMargin; at += shape.branchEvery) {
        if (random(layout) < shape.branchChance) {
            spots.append({at, side});
            side = -side;
        }
    }
    return spots;
}

void QcVillagePlanner::layBranches(Layout &layout, const QList<QList<Road>> &trunks, QList<Road> &roads) const
{
    for (const QList<Road> &trunk : trunks) {
        const QList<std::pair<int, int>> spots = branchSpots(layout, trunk);
        for (const auto &[at, side] : spots) {
            const auto run = std::ranges::find_if(trunk, [at](const Road &r) { return at >= r.armOffset && at < r.armOffset + r.length; });
            const int along = at - run->armOffset;
            const int direction = int(std::ranges::find(directions, std::pair{run->dx, run->dz}) - directions.begin());
            Arm branch;
            branch.x = run->x + run->dx * along;
            branch.z = run->z + run->dz * along;
            branch.direction = (direction + 2 * side + 8) % 8;
            branch.length = int(m_settings.radius * drawShare(layout, layout.shape->branchLength));
            branch.firstSlot = roadHalfWidth + 1;
            branch.branch = true;
            branch.winding = layout.winding;
            layArms(layout, {branch}, roads, nullptr);
        }
    }
}
