#include "worldgen/villages/QcRoadNetworkPlanner.h"

#include <algorithm>

namespace {

using Kind = QcVillageLayout::Kind;
constexpr auto &directions = QcVillageDraft::directions;
constexpr int roadHalfWidth = QcVillageDraft::roadHalfWidth;

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

QcRoadNetworkPlanner::QcRoadNetworkPlanner(const QcTerrainGenerator &terrain, const QcContentRegistry &content)
    : m_layer(terrain, content)
{
}

int QcRoadNetworkPlanner::drawCount(QcVillageDraft &draft, QcVillageRange<int> range)
{
    return range.min + std::min(range.max - range.min, static_cast<int>(draft.random() * static_cast<double>(range.max - range.min + 1)));
}

double QcRoadNetworkPlanner::drawShare(QcVillageDraft &draft, QcVillageRange<double> range)
{
    return range.min + (range.max - range.min) * draft.random();
}

QcVillageDraft::Layout QcRoadNetworkPlanner::chooseLayout(QcVillageDraft &draft)
{
    const QList<QcVillageLayout> &layouts = draft.style->layouts;
    int total = 0;
    for (const QcVillageLayout &entry : layouts) {
        total += entry.weight;
    }
    int pick = static_cast<int>(draft.random() * total);
    QcVillageDraft::Layout layout;
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
    const double draw = draft.random();
    if (shape.diagonals && shape.kind == Kind::Street) {
        layout.mainDirection = static_cast<int>(draw * 8.0);
    } else if (shape.diagonals && shape.kind == Kind::Tree) {
        layout.mainDirection = 2 * static_cast<int>(draw * 4.0) + 1;
    } else {
        layout.mainDirection = 2 * static_cast<int>(draw * 4.0);
    }
    layout.winding = shape.diagonals && draft.random() < shape.winding;
    return layout;
}

QcRoadNetworkPlanner::Arm QcRoadNetworkPlanner::fromCentre(QcVillageDraft &draft, int direction) const
{
    Arm arm;
    arm.x = draft.centreX;
    arm.z = draft.centreZ;
    arm.direction = direction % 8;
    arm.length = static_cast<int>(draft.region.radius * drawShare(draft, draft.shape->armLength));
    arm.firstSlot = clearOfCentre(draft, arm.direction);
    arm.winding = draft.winding;
    return arm;
}

QList<QcRoadNetworkPlanner::Road> QcRoadNetworkPlanner::layRoads(QcVillageDraft &draft) const
{
    const int main = draft.mainDirection;
    Network network;
    QList<Arm> arms;
    switch (draft.shape->kind) {
    case Kind::Street:
        arms = {fromCentre(draft, main), fromCentre(draft, main + 4)};
        break;
    case Kind::Tee:
        arms = {fromCentre(draft, main), fromCentre(draft, main + 2), fromCentre(draft, main + 6)};
        break;
    case Kind::Ell:
        arms = {fromCentre(draft, main), fromCentre(draft, main + (draft.random() < 0.5 ? 2 : 6))};
        break;
    case Kind::Tree:
        arms = treeArms(draft);
        break;
    case Kind::Plaza:
        if (std::optional<Network> plaza = layPlaza(draft)) {
            network = std::move(*plaza);
            break;
        }
        // A ring cut by water or a steep step: the village is a cross instead.
        draft.setLayoutKind(Kind::Cross);
        [[fallthrough]];
    case Kind::Cross:
        arms = {fromCentre(draft, main), fromCentre(draft, main + 2), fromCentre(draft, main + 4), fromCentre(draft, main + 6)};
        break;
    }
    const QList<QList<Road>> laid = layArms(draft, arms);
    for (const QList<Road> &runs : laid) {
        network.roads += runs;
        network.trunks.append(runs);
    }
    return network.roads + layBranches(draft, network.trunks);
}

QList<QList<QcRoadNetworkPlanner::Road>> QcRoadNetworkPlanner::layArms(QcVillageDraft &draft, const QList<Arm> &arms) const
{
    QList<QList<Road>> laid;
    for (const Arm &arm : arms) {
        QList<Road> runs = m_layer.layArm(draft, arm);
        if (!runs.isEmpty()) {
            laid.append(std::move(runs));
        }
    }
    return laid;
}

int QcRoadNetworkPlanner::clearOfCentre(const QcVillageDraft &draft, int direction)
{
    const QcVillageDraft::Rect centre = draft.pieces.front();
    const auto [dx, dz] = directions[direction];
    int d = 1;
    while (d < draft.region.radius
           && centre.intersects(QcVillageDraft::brush({draft.centreX + dx * d, draft.centreZ + dz * d}, dx, dz, dx != 0 && dz != 0), 0)) {
        ++d;
    }
    return d;
}

std::optional<QcRoadNetworkPlanner::Network> QcRoadNetworkPlanner::layPlaza(QcVillageDraft &draft) const
{
    // The ring's inner edge keeps two blocks of grass to the centre piece on every side.
    int clear = 0;
    for (int direction = 0; direction < 8; direction += 2) {
        clear = std::max(clear, clearOfCentre(draft, direction));
    }
    const int ring = std::max(drawCount(draft, draft.shape->ring), clear + 3);

    // A side road from the centre to the ring, without pieces, towards the main direction or else
    // the first side it reaches the ring on in full; without one the plaza is left out.
    Arm spoke = fromCentre(draft, draft.mainDirection);
    spoke.length = ring;
    spoke.branch = true;
    spoke.winding = false;
    spoke.closed = true;
    spoke.sides = 0;
    int turn = 0;
    while (turn < 8 && m_layer.walkArm(draft, spoke).isEmpty()) {
        turn += 2;
        spoke.direction = (draft.mainDirection + turn) % 8;
        spoke.firstSlot = clearOfCentre(draft, spoke.direction);
    }
    if (turn == 8) {
        return std::nullopt;
    }

    Arm loop;
    loop.x = draft.centreX - ring;
    loop.z = draft.centreZ - ring;
    loop.length = 8 * ring;
    loop.bends = {{2 * ring + 1, 2}, {4 * ring + 1, 2}, {6 * ring + 1, 2}};
    loop.closed = true;
    // Clockwise, the left side faces out: the square inside stays open.
    loop.sides = 1;
    Network plaza;
    plaza.roads = m_layer.layArm(draft, loop);
    if (plaza.roads.isEmpty()) {
        return std::nullopt;
    }

    QList<int> sides{draft.mainDirection, draft.mainDirection + 2, draft.mainDirection + 4, draft.mainDirection + 6};
    const int exits = drawCount(draft, draft.shape->exits);
    QList<Arm> arms;
    for (int n = 0; n < exits && !sides.isEmpty(); ++n) {
        const int direction = sides.takeAt(std::min(sides.size() - 1, static_cast<qsizetype>(draft.random() * static_cast<double>(sides.size())))) % 8;
        const auto [dx, dz] = directions[direction];
        // Off the middle of its side, so the exits and the spoke never line up into a cross.
        const int shift = exitShift(draft, ring);
        Arm exit;
        exit.x = draft.centreX + dx * ring + dz * shift;
        exit.z = draft.centreZ + dz * ring + dx * shift;
        exit.direction = direction;
        exit.length = static_cast<int>(draft.region.radius * drawShare(draft, draft.shape->armLength)) - ring;
        exit.firstSlot = roadHalfWidth + 1;
        exit.winding = draft.winding;
        if (exit.length >= exitMinimum) {
            arms.append(exit);
        }
    }
    const QList<QList<Road>> laidExits = layArms(draft, arms);
    for (const QList<Road> &runs : laidExits) {
        plaza.roads += runs;
        plaza.trunks.append(runs);
    }
    const QList<QList<Road>> spokes = layArms(draft, {spoke});
    for (const QList<Road> &runs : spokes) {
        plaza.roads += runs;
    }
    return plaza;
}

int QcRoadNetworkPlanner::exitShift(QcVillageDraft &draft, int ring) const
{
    // Two to ring / 2 blocks to either side; the corners of the ring stay clear.
    const int most = std::max(2, std::min(ring / 2, ring - roadHalfWidth - 2));
    const int shift = 2 + static_cast<int>(draft.random() * static_cast<double>(most - 1));
    return draft.random() < 0.5 ? -shift : shift;
}

QList<QcRoadNetworkPlanner::Arm> QcRoadNetworkPlanner::treeArms(QcVillageDraft &draft) const
{
    QList<Arm> arms;
    const QList<int> headings = treeDirections(draft);
    for (const int direction : headings) {
        Arm arm = fromCentre(draft, direction);
        const int bends = drawCount(draft, draft.shape->bends);
        for (int k = 0; k < bends; ++k) {
            const int at = arm.firstSlot + (arm.length - arm.firstSlot) * (k + 1) / (bends + 1);
            const int turn = (draft.random() < 0.5 ? -1 : 1) * (draft.shape->diagonals ? 1 : 2);
            arm.bends.append({at, turn});
        }
        arms.append(arm);
    }
    return arms;
}

QList<int> QcRoadNetworkPlanner::treeDirections(QcVillageDraft &draft) const
{
    const bool diagonals = draft.shape->diagonals;
    const int count = std::min(drawCount(draft, draft.shape->arms), diagonals ? 8 : 4);
    QList<int> chosen{draft.mainDirection};
    for (int attempt = 0; attempt < treeAttempts && chosen.size() < count; ++attempt) {
        const int direction = diagonals ? static_cast<int>(draft.random() * 8.0) : 2 * static_cast<int>(draft.random() * 4.0);
        const int apart = attempt < treeAttempts / 2 ? 2 : 1;
        if (std::ranges::all_of(chosen, [&](int other) { return turnDistance(direction, other) >= apart; })) {
            chosen.append(direction);
        }
    }
    return chosen;
}

QList<std::pair<int, int>> QcRoadNetworkPlanner::branchSpots(QcVillageDraft &draft, const QList<Road> &trunk) const
{
    const QcVillageLayout &shape = *draft.shape;
    const int length = trunk.back().armOffset + trunk.back().length;
    QList<std::pair<int, int>> spots;
    if (shape.branchEvery == 0) {
        if (length >= branchMinimum && draft.random() < shape.branchChance) {
            const int at = static_cast<int>(length * (0.5 + 0.2 * draft.random()));
            spots.append({at, draft.random() < 0.5 ? -1 : 1});
        }
        return spots;
    }

    // Every few blocks, the sides taking turns.
    int side = draft.random() < 0.5 ? -1 : 1;
    for (int at = trunk.front().firstSlot + shape.branchEvery / 2; at < length - branchEndMargin; at += shape.branchEvery) {
        if (draft.random() < shape.branchChance) {
            spots.append({at, side});
            side = -side;
        }
    }
    return spots;
}

QList<QcRoadNetworkPlanner::Road> QcRoadNetworkPlanner::layBranches(QcVillageDraft &draft, const QList<QList<Road>> &trunks) const
{
    QList<Road> roads;
    for (const QList<Road> &trunk : trunks) {
        const QList<std::pair<int, int>> spots = branchSpots(draft, trunk);
        for (const auto &[at, side] : spots) {
            const auto run = std::ranges::find_if(trunk, [at](const Road &r) { return at >= r.armOffset && at < r.armOffset + r.length; });
            const int along = at - run->armOffset;
            const int direction = static_cast<int>(std::ranges::find(directions, std::pair{run->dx, run->dz}) - directions.begin());
            Arm branch;
            branch.x = run->x + run->dx * along;
            branch.z = run->z + run->dz * along;
            branch.direction = (direction + 2 * side + 8) % 8;
            branch.length = static_cast<int>(draft.region.radius * drawShare(draft, draft.shape->branchLength));
            branch.firstSlot = roadHalfWidth + 1;
            branch.branch = true;
            branch.winding = draft.winding;
            const QList<QList<Road>> laid = layArms(draft, {branch});
            for (const QList<Road> &runs : laid) {
                roads += runs;
            }
        }
    }
    return roads;
}
