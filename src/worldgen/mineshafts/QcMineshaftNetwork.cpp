#include "worldgen/mineshafts/QcMineshaftNetwork.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace {

// Staircases keep off the lattice point of the central room on every level: the room is wider than
// a crossing, so a staircase beside it would cut through its floor.
bool besideRoom(QcMineshaftNetwork::Point a, QcMineshaftNetwork::Point b)
{
    return (a.x == 0 && a.z == 0) || (b.x == 0 && b.z == 0);
}

// North, east, south, west.
constexpr std::array<std::pair<int, int>, 4> directions{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};

// Purposes of the random values.
enum Purpose : int {
    GrowNode = 1,
    GrowDirection,
    GrowLoop,
    NodeCount,
    StairsNode,
    StairsDirection,
    ExtraStairs,
    ExtraNode,
    ExtraDirection,
};

} // namespace

QcMineshaftNetwork::QcMineshaftNetwork(const QcMineshaftSettings &settings, Random random, NodeJudge nodeJudge, LinkJudge linkJudge)
    : m_settings(settings)
    , m_random(std::move(random))
    , m_nodeJudge(std::move(nodeJudge))
    , m_linkJudge(std::move(linkJudge))
{
}

int QcMineshaftNetwork::grow(int levels, bool roomBridges)
{
    m_nodes.append({{0, 0, 0}, roomBridges});
    occupy(elementKey(Point{}), {0}, roomBridges);
    growLevel(0);

    int made = 1;
    while (made < levels && addStairsDown(made - 1)) {
        growLevel(made);
        addExtraStairs(made - 1);
        ++made;
    }
    return made;
}

std::optional<int> QcMineshaftNetwork::nodeAt(Point at) const
{
    const auto it = std::ranges::find(m_nodes, at, &Node::at);
    return it == m_nodes.end() ? std::nullopt : std::optional<int>(static_cast<int>(it - m_nodes.begin()));
}

int QcMineshaftNetwork::degree(int node) const
{
    return static_cast<int>(std::ranges::count_if(m_links, [node](Link link) { return link.from == node || link.to == node; }));
}

bool QcMineshaftNetwork::bridgedElsewhere(std::pair<int, int> key, int level) const
{
    const QList<Use> uses = m_uses.value(key);
    return std::ranges::any_of(uses, [level](Use use) { return use.level != level && use.bridge; });
}

bool QcMineshaftNetwork::free(std::pair<int, int> key, std::initializer_list<int> levels, bool bridge) const
{
    const QList<Use> uses = m_uses.value(key);
    return std::ranges::none_of(uses, [&](Use use) { return std::ranges::find(levels, use.level) != levels.end() || bridge || use.bridge; });
}

void QcMineshaftNetwork::occupy(std::pair<int, int> key, std::initializer_list<int> levels, bool bridge)
{
    for (const int level : levels) {
        m_uses[key].append({level, bridge});
    }
}

bool QcMineshaftNetwork::linked(int a, int b) const
{
    return std::ranges::any_of(m_links, [a, b](Link link) { return (link.from == a && link.to == b) || (link.from == b && link.to == a); });
}

bool QcMineshaftNetwork::inside(Point at) const
{
    return std::max(std::abs(at.x), std::abs(at.z)) <= m_settings.radius;
}

QList<int> QcMineshaftNetwork::nodesOf(int level) const
{
    QList<int> found;
    for (qsizetype i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].at.level == level) {
            found.append(static_cast<int>(i));
        }
    }
    return found;
}

bool QcMineshaftNetwork::tryLink(int from, Point to, std::optional<int> existing)
{
    const Point at = m_nodes[from].at;
    const Verdict link = m_linkJudge(at, to);
    const std::pair<int, int> key = elementKey(at, to);
    if (!link.fits || !free(key, {to.level}, link.bridge)) {
        return false;
    }

    int target = existing.value_or(-1);
    if (!existing) {
        const Verdict node = m_nodeJudge(to);
        if (!node.fits || !free(elementKey(to), {to.level}, node.bridge)) {
            return false;
        }
        target = static_cast<int>(m_nodes.size());
        m_nodes.append({to, node.bridge});
        occupy(elementKey(to), {to.level}, node.bridge);
    }
    m_links.append({from, target, false, link.bridge});
    occupy(key, {to.level}, link.bridge);
    return true;
}

void QcMineshaftNetwork::growLevel(int level)
{
    const int span = m_settings.maxNodes - m_settings.minNodes + 1;
    const int target = m_settings.minNodes + std::min(span - 1, static_cast<int>(m_random(NodeCount, level) * span));
    QList<int> members = nodesOf(level);
    for (int attempt = 0; attempt < 8 * target && members.size() < target; ++attempt) {
        const int index = level * 1000 + attempt;
        const int from = members[std::min(members.size() - 1, static_cast<qsizetype>(m_random(GrowNode, index) * static_cast<double>(members.size())))];
        const auto [dx, dz] = directions[static_cast<std::size_t>(std::min(3, static_cast<int>(m_random(GrowDirection, index) * 4)))];
        const Point to{m_nodes[from].at.x + dx, m_nodes[from].at.z + dz, level};
        if (!inside(to)) {
            continue;
        }

        const std::optional<int> existing = nodeAt(to);
        if (existing) {
            // A loop: two nodes joined another way already.
            if (!linked(from, *existing) && m_random(GrowLoop, index) < m_settings.loopChance) {
                tryLink(from, to, existing);
            }
            continue;
        }
        if (tryLink(from, to, std::nullopt)) {
            members.append(static_cast<int>(m_nodes.size() - 1));
        }
    }
}

bool QcMineshaftNetwork::addStairsDown(int level)
{
    const QList<int> members = nodesOf(level);
    for (int attempt = 0; attempt < 32; ++attempt) {
        const int index = level * 1000 + attempt;
        const int from = members[std::min(members.size() - 1, static_cast<qsizetype>(m_random(StairsNode, index) * static_cast<double>(members.size())))];
        const auto [dx, dz] = directions[static_cast<std::size_t>(std::min(3, static_cast<int>(m_random(StairsDirection, index) * 4)))];
        const Point at = m_nodes[from].at;
        const Point to{at.x + dx, at.z + dz, level + 1};
        if (besideRoom(at, to) || !inside(to)) {
            continue;
        }

        const Verdict link = m_linkJudge(at, to);
        const Verdict node = m_nodeJudge(to);
        const std::pair<int, int> key = elementKey(at, to);
        if (!link.fits || !node.fits || !free(key, {level, level + 1}, link.bridge) || !free(elementKey(to), {level + 1}, node.bridge)) {
            continue;
        }
        m_nodes.append({to, node.bridge});
        occupy(elementKey(to), {level + 1}, node.bridge);
        m_links.append({from, static_cast<int>(m_nodes.size() - 1), true, link.bridge});
        occupy(key, {level, level + 1}, link.bridge);
        return true;
    }
    return false;
}

void QcMineshaftNetwork::addExtraStairs(int level)
{
    if (m_random(ExtraStairs, level) >= m_settings.extraStairChance) {
        return;
    }
    const QList<int> members = nodesOf(level);
    for (int attempt = 0; attempt < 32; ++attempt) {
        const int index = level * 1000 + attempt;
        const int from = members[std::min(members.size() - 1, static_cast<qsizetype>(m_random(ExtraNode, index) * static_cast<double>(members.size())))];
        const auto [dx, dz] = directions[static_cast<std::size_t>(std::min(3, static_cast<int>(m_random(ExtraDirection, index) * 4)))];
        const Point at = m_nodes[from].at;
        const std::optional<int> to = nodeAt({at.x + dx, at.z + dz, level + 1});
        if (!to || besideRoom(at, m_nodes[*to].at) || linked(from, *to)) {
            continue;
        }

        const Verdict link = m_linkJudge(at, m_nodes[*to].at);
        const std::pair<int, int> key = elementKey(at, m_nodes[*to].at);
        if (link.fits && free(key, {level, level + 1}, link.bridge)) {
            m_links.append({from, *to, true, link.bridge});
            occupy(key, {level, level + 1}, link.bridge);
            return;
        }
    }
}
