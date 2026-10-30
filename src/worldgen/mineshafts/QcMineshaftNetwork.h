// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcMineshaftDefinition.h"

#include <QHash>
#include <QList>

#include <functional>
#include <optional>

/*! \brief The lattice graph of a mine shaft (decision E-570), before any block: points (x, z) of a
    square lattice on levels (0 the top), nodes at some of them, corridors between neighbouring
    nodes of a level (grown in straight runs of up to \c {maxRun} steps, decision E-889) and
    staircases from a node to the neighbouring lattice point one level lower.

    Level 0 grows from the central room at (0, 0); every further level grows from the lower end
    of the first staircase down to it, so every node is reachable from the room. A judge (the
    planner, which knows the terrain) decides whether a node or a link fits and whether it
    bridges a cave. Two elements on the same lattice point or edge never share a level, and where
    one of them is a bridge, no other level may use that point or edge at all (its pillars would
    pass through). */
class QcMineshaftNetwork
{
public:
    /*! \brief A lattice point: \c {x} and \c {z} in lattice steps from the central room, and
        the \c {level} (0 the top, counting downwards). \qcfields */
    struct Point
    {
        int x = 0;
        int z = 0;
        int level = 0;

        bool operator==(const Point &) const = default;
    };

    /*! \brief A corridor or staircase between two nodes. */
    struct Link
    {
        /*! Index of the first node in nodes(). */
        int from = 0;
        /*! Index of the second node in nodes(). */
        int to = 0;
        /*! A staircase: \c {from} on a level, \c {to} one lower. */
        bool stairs = false;
        /*! Whether it bridges a cave (the link judge's verdict). */
        bool bridge = false;
    };

    /*! \brief A node at a lattice point. */
    struct Node
    {
        /*! Its lattice point. */
        Point at;
        /*! Whether it bridges a cave (the node judge's verdict). */
        bool bridge = false;
    };

    /*! \brief Whether an element may be made, and whether it bridges a cave. */
    struct Verdict
    {
        /*! Whether the element may be made. */
        bool fits = false;
        /*! Whether it bridges a cave. */
        bool bridge = false;
    };

    /*! The judge's question for a node at a point. */
    using NodeJudge = std::function<Verdict(Point)>;
    /*! The judge's question for a link between two points (a staircase when the second is one
        level lower). */
    using LinkJudge = std::function<Verdict(Point, Point)>;
    /*! A random value in [0, 1) for a purpose and an index. */
    using Random = std::function<double(int, int)>;

    QcMineshaftNetwork(const QcMineshaftSettings &settings, Random random, NodeJudge nodeJudge, LinkJudge linkJudge);

    /*! Grows up to \a levels levels from the room (node 0, which must fit; \a roomBridges:
        whether it bridges a cave); returns the levels made. A further level is made only while
        a first staircase down to it fits. */
    int grow(int levels, bool roomBridges);

    /*! Returns the nodes grown, the central room first. */
    const QList<Node> &nodes() const { return m_nodes; }
    /*! Returns the corridors and staircases grown. */
    const QList<Link> &links() const { return m_links; }
    /*! Returns the index of the node at the lattice point \a at, std::nullopt without one. */
    std::optional<int> nodeAt(Point at) const;
    /*! Returns the number of links at the node with index \a node. */
    int degree(int node) const;
    /*! Returns whether an element of a level other than \a level bridges a cave at the lattice
        point or edge with the key \a key (elementKey()). */
    bool bridgedElsewhere(std::pair<int, int> key, int level) const;
    /*! Returns the key of the edge between the neighbouring points \a a and \a b: their sum, a
        point in doubled coordinates (levels ignored). */
    static std::pair<int, int> elementKey(Point a, Point b) { return {a.x + b.x, a.z + b.z}; }
    /*! Returns the key of the lattice point \a a: its x and z doubled (level ignored). */
    static std::pair<int, int> elementKey(Point a) { return {2 * a.x, 2 * a.z}; }

    /*! \brief A level's element on a lattice point or edge: its \c {level} and whether it is a
        \c {bridge}. \qcfields */
    struct Use
    {
        int level = 0;
        bool bridge = false;
    };

private:
    // How one try of growLevel() goes on from a node: new nodes at most, and whether it may join an
    // existing node it meets.
    struct Run
    {
        int steps = 1;
        bool loop = false;
    };

    void growLevel(int level);
    // Grows straight on from node `from` along `direction` (decision E-889); returns the new nodes.
    [[nodiscard]] QList<int> growRun(int from, std::pair<int, int> direction, Run run);
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
