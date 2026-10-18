#pragma once

#include "common/content/definitions/QcMineshaftDefinition.h"

#include <QHash>
#include <QList>

#include <functional>
#include <optional>

// The lattice graph of a mine shaft (decision E-570), before any block: points (x, z) of a square
// lattice on levels (0 the top), nodes at some of them, corridors between neighbouring nodes of a
// level and staircases from a node to the neighbouring lattice point one level lower. Level 0
// grows from the central room at (0, 0); every further level grows from the lower end of the first
// staircase down to it, so every node is reachable from the room. A judge (the planner, which knows
// the terrain) decides whether a node or a link fits and whether it bridges a cave. Two elements on
// the same lattice point or edge never share a level, and where one of them is a bridge, no other
// level may use that point or edge at all (its pillars would pass through).
class QcMineshaftNetwork
{
public:
    struct Point
    {
        int x = 0;
        int z = 0;
        int level = 0;

        bool operator==(const Point &) const = default;
    };

    struct Link
    {
        int from = 0;
        int to = 0;
        // A staircase: `from` on a level, `to` one lower.
        bool stairs = false;
        bool bridge = false;
    };

    struct Node
    {
        Point at;
        bool bridge = false;
    };

    // Whether an element may be made, and whether it bridges a cave.
    struct Verdict
    {
        bool fits = false;
        bool bridge = false;
    };

    // The judge's questions: a node at a point, or a link between two points (a staircase when the
    // second is one level lower).
    using NodeJudge = std::function<Verdict(Point)>;
    using LinkJudge = std::function<Verdict(Point, Point)>;
    // A random value in [0, 1) for a purpose and an index.
    using Random = std::function<double(int, int)>;

    QcMineshaftNetwork(const QcMineshaftSettings &settings, Random random, NodeJudge nodeJudge, LinkJudge linkJudge);

    // Grows up to `levels` levels from the room (node 0, which must fit); returns the levels made.
    int grow(int levels, bool roomBridges);

    const QList<Node> &nodes() const { return m_nodes; }
    const QList<Link> &links() const { return m_links; }
    std::optional<int> nodeAt(Point at) const;
    // Links at a node.
    int degree(int node) const;
    // Whether an element of a level other than `level` bridges a cave at the lattice point or edge
    // with this key (elementKey).
    bool bridgedElsewhere(std::pair<int, int> key, int level) const;
    // The key of a lattice point, or of the edge between two neighbouring points: their sum in
    // doubled coordinates.
    static std::pair<int, int> elementKey(Point a, Point b) { return {a.x + b.x, a.z + b.z}; }
    static std::pair<int, int> elementKey(Point a) { return {2 * a.x, 2 * a.z}; }

    // A level's element on a lattice point or edge.
    struct Use
    {
        int level = 0;
        bool bridge = false;
    };

private:
    void growLevel(int level);
    // A first staircase from level `level` down to a new node of the next level.
    bool addStairsDown(int level);
    // A second staircase between nodes of `level` and the next level, with the settings' chance.
    void addExtraStairs(int level);
    // Adds a link to a new node at `to` (or to the existing node `existing`).
    bool tryLink(int from, Point to, std::optional<int> existing);
    bool free(std::pair<int, int> key, std::initializer_list<int> levels, bool bridge) const;
    void occupy(std::pair<int, int> key, std::initializer_list<int> levels, bool bridge);
    bool linked(int a, int b) const;
    bool inside(Point at) const;
    QList<int> nodesOf(int level) const;

    const QcMineshaftSettings &m_settings;
    Random m_random;
    NodeJudge m_nodeJudge;
    LinkJudge m_linkJudge;
    QList<Node> m_nodes;
    QList<Link> m_links;
    QHash<std::pair<int, int>, QList<Use>> m_uses;
};

Q_DECLARE_TYPEINFO(QcMineshaftNetwork::Use, Q_PRIMITIVE_TYPE);
Q_DECLARE_TYPEINFO(QcMineshaftNetwork::Link, Q_PRIMITIVE_TYPE);
Q_DECLARE_TYPEINFO(QcMineshaftNetwork::Node, Q_PRIMITIVE_TYPE);
