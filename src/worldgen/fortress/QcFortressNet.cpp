// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/fortress/QcFortressNet.h"

#include <algorithm>
#include <cstdlib>
#include <ranges>

namespace {

using Side = QcFortressPlan::Side;
using RoomKind = QcFortressPlan::RoomKind;

// Tries per node of growing the net.
constexpr int growthTries = 12;

Side sideFrom(double roll)
{
    return static_cast<Side>(std::min(3, static_cast<int>(roll * 4.0)));
}

int indexFrom(double roll, qsizetype count)
{
    return static_cast<int>(std::min(count - 1, static_cast<qsizetype>(roll * static_cast<double>(count))));
}

} // namespace

int QcFortressNet::addNode(Node node)
{
    m_nodes.append(node);
    return static_cast<int>(m_nodes.size()) - 1;
}

std::optional<int> QcFortressNet::nodeAt(int x, int z, int level) const
{
    for (qsizetype i = 0; i < m_nodes.size(); ++i) {
        const Node &node = m_nodes[i];
        if (node.x == x && node.z == z && node.level == level) {
            return static_cast<int>(i);
        }
    }
    return std::nullopt;
}

int QcFortressNet::degree(int node) const
{
    return static_cast<int>(std::ranges::count_if(m_links, [node](Link link) { return link.from == node || link.to == node; }));
}

bool QcFortressNet::linked(int a, int b) const
{
    return std::ranges::any_of(m_links, [a, b](Link link) { return (link.from == a && link.to == b) || (link.from == b && link.to == a); });
}

bool QcFortressNet::edgeUsed(Node a, Node b) const
{
    const auto isEdge = [&](Link link) {
        const Node &from = m_nodes[link.from];
        const Node &to = m_nodes[link.to];
        const bool same = from.x == a.x && from.z == a.z && to.x == b.x && to.z == b.z;
        const bool swapped = from.x == b.x && from.z == b.z && to.x == a.x && to.z == a.z;
        return same || swapped;
    };
    return std::ranges::any_of(m_links, isEdge);
}

QcFortressPlan::Side QcFortressNet::sideBetween(int from, int to) const
{
    const Node &a = m_nodes[from];
    const Node &b = m_nodes[to];
    if (a.x == b.x) {
        return b.z < a.z ? Side::North : Side::South;
    }
    return b.x > a.x ? Side::East : Side::West;
}

void QcFortressNet::growLevel(int first, int count, const Limits &limits, const Roll &roll)
{
    const int level = m_nodes[first].level;
    int added = 0;
    for (int tries = 0; tries < count * growthTries && added < count; ++tries) {
        // The newest nodes first, so the net runs out in arms instead of filling the lattice.
        const double recent = roll();
        const int span = static_cast<int>(m_nodes.size()) - first;
        const int from = static_cast<int>(m_nodes.size()) - 1 - indexFrom(recent * recent, span);
        const Side side = sideFrom(roll());
        const auto [stepX, stepZ] = QcFortressPlan::stepOf(side);
        const int x = m_nodes[from].x + stepX;
        const int z = m_nodes[from].z + stepZ;
        const bool flightSide = level == 1 && from == first && side == m_flight;
        if (x < 0 || z < 0 || x >= limits.lattice || z >= limits.lattice || nodeAt(x, z, level) || flightSide) {
            continue;
        }
        const Node next{x, z, level, RoomKind::Crossing};
        if (edgeUsed(m_nodes[from], next)) {
            continue;
        }
        m_links.append({from, addNode(next)});
        ++added;
    }
}

void QcFortressNet::closeLoops(const Limits &limits, const Roll &roll)
{
    const qsizetype count = m_nodes.size();
    for (qsizetype i = 0; i < count; ++i) {
        for (const Side side : {Side::East, Side::South}) {
            const auto [stepX, stepZ] = QcFortressPlan::stepOf(side);
            const Node &node = m_nodes[i];
            const std::optional<int> other = nodeAt(node.x + stepX, node.z + stepZ, 0);
            if (other && !linked(static_cast<int>(i), *other) && roll() < limits.loopChance) {
                m_links.append({static_cast<int>(i), *other});
            }
        }
    }
}

int QcFortressNet::placeStairwell(const Roll &roll)
{
    // Inside the net: the node with the most links but one free wall on the upper floor.
    QList<int> candidates;
    for (qsizetype i = 0; i < m_nodes.size(); ++i) {
        if (degree(static_cast<int>(i)) >= 2) {
            candidates.append(static_cast<int>(i));
        }
    }
    const int lower = candidates.isEmpty() ? 0 : candidates[indexFrom(roll(), candidates.size())];
    m_nodes[lower].kind = RoomKind::Stairwell;
    m_flight = sideFrom(roll());
    return addNode({m_nodes[lower].x, m_nodes[lower].z, 1, RoomKind::Stairwell});
}

void QcFortressNet::placeStaircase(const Roll &roll)
{
    QList<Link> free;
    for (qsizetype i = 0; i < m_nodes.size(); ++i) {
        const Node &upper = m_nodes[i];
        if (upper.level != 1 || upper.kind == RoomKind::Stairwell) {
            continue;
        }
        for (const Side side : {Side::North, Side::East, Side::South, Side::West}) {
            const auto [stepX, stepZ] = QcFortressPlan::stepOf(side);
            const std::optional<int> lower = nodeAt(upper.x + stepX, upper.z + stepZ, 0);
            if (lower && m_nodes[*lower].kind == RoomKind::Crossing && !edgeUsed(upper, m_nodes[*lower])) {
                free.append({*lower, static_cast<int>(i)});
            }
        }
    }
    if (!free.isEmpty()) {
        m_links.append(free[indexFrom(roll(), free.size())]);
    }
}

void QcFortressNet::assignKinds()
{
    // Far from the middle, without a room above them; dead ends before others of the same reach.
    const Node &middle = m_nodes.constFirst();
    const auto rank = [&](int index) {
        const Node &node = m_nodes[index];
        const int reach = std::abs(node.x - middle.x) + std::abs(node.z - middle.z);
        return reach * 2 + (degree(index) == 1 ? 1 : 0);
    };
    QList<int> free;
    for (qsizetype i = 1; i < m_nodes.size(); ++i) {
        const Node &node = m_nodes[i];
        if (node.level == 0 && node.kind == RoomKind::Crossing && !nodeAt(node.x, node.z, 1)) {
            free.append(static_cast<int>(i));
        }
    }
    std::ranges::stable_sort(free, std::ranges::greater{}, rank);
    if (!free.isEmpty()) {
        m_nodes[free.takeFirst()].kind = RoomKind::Platform;
    }
    if (!free.isEmpty()) {
        m_nodes[free.takeFirst()].kind = RoomKind::Garden;
    }
}

QcFortressNet QcFortressNet::grow(const Limits &limits, const Roll &roll)
{
    QcFortressNet net;
    const int centre = limits.lattice / 2;
    const int first = net.addNode({centre, centre, 0, RoomKind::Crossing});
    const int rooms = limits.minRooms + indexFrom(roll(), limits.maxRooms - limits.minRooms + 1);
    net.growLevel(first, rooms - 1, limits, roll);
    net.closeLoops(limits, roll);

    const int stairwell = net.placeStairwell(roll);
    net.growLevel(stairwell, limits.upperRooms, limits, roll);
    net.placeStaircase(roll);
    net.assignKinds();
    return net;
}
