// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/fortress/QcFortressPlan.h"

#include <QList>

#include <functional>
#include <optional>

/*! \brief The graph of a fortress (decision E-1071), before any block: points of a square lattice
    on two levels, with nodes at some of them and links between neighbouring points.

    The lower level grows from the middle of the lattice, a few links close loops; one node is
    doubled into a stairwell from which the upper level grows, and a link from the upper level
    down to a neighbouring point of the lower one makes a staircase. A lattice edge holds one link
    on whatever level, so no passage runs above another and nothing but a room stands on a room. */
class QcFortressNet
{
public:
    /*! Rolls in 0 to 1 that decide the shape, one per call. */
    using Roll = std::function<double()>;

    /*! \brief How large the net may grow. \qcfields */
    struct Limits
    {
        int lattice = 5;
        int minRooms = 10;
        int maxRooms = 16;
        int upperRooms = 4;
        double loopChance = 0.25;
    };

    /*! \brief A node at the lattice point \c {x}, \c {z} on \c {level} (0 or 1). \qcfields */
    struct Node
    {
        int x = 0;
        int z = 0;
        int level = 0;
        QcFortressPlan::RoomKind kind = QcFortressPlan::RoomKind::Crossing;
    };

    /*! \brief A link between the nodes \c {from} and \c {to} (indices, the lower end first).
        \qcfields */
    struct Link
    {
        int from = 0;
        int to = 0;
    };

    /*! Returns the net grown from \a limits with \a roll. */
    [[nodiscard]] static QcFortressNet grow(const Limits &limits, const Roll &roll);

    /*! Returns the nodes, the first in the middle of the lattice. */
    [[nodiscard]] const QList<Node> &nodes() const { return m_nodes; }
    /*! Returns the links. */
    [[nodiscard]] const QList<Link> &links() const { return m_links; }
    /*! Returns the node at the point \a x, \a z on \a level, if any. */
    [[nodiscard]] std::optional<int> nodeAt(int x, int z, int level) const;
    /*! Returns the side of the node \a from on which the lattice leads to \a to. */
    [[nodiscard]] QcFortressPlan::Side sideBetween(int from, int to) const;
    /*! Returns the wall of the stairwell's flight. */
    [[nodiscard]] QcFortressPlan::Side flight() const { return m_flight; }

private:
    [[nodiscard]] int addNode(Node node);
    [[nodiscard]] int degree(int node) const;
    [[nodiscard]] bool linked(int a, int b) const;
    // Whether a link uses the lattice edge between two points, on whatever level.
    [[nodiscard]] bool edgeUsed(Node a, Node b) const;
    // Grows `count` nodes more on the level of the nodes from index `first` on.
    void growLevel(int first, int count, const Limits &limits, const Roll &roll);
    void closeLoops(const Limits &limits, const Roll &roll);
    // Doubles a node into the stairwell; returns the index of its upper node.
    [[nodiscard]] int placeStairwell(const Roll &roll);
    // Links an upper node down to a neighbouring lower node, if an edge is free.
    void placeStaircase(const Roll &roll);
    // Gives the farthest lower dead ends the platform and the garden.
    void assignKinds();

    QList<Node> m_nodes; // NOLINT(qc-member-access): grown only inside grow(), fixed afterwards
    QList<Link> m_links; // NOLINT(qc-member-access): grown only inside grow(), fixed afterwards
    QcFortressPlan::Side m_flight = QcFortressPlan::Side::North;
};

Q_DECLARE_TYPEINFO(QcFortressNet::Link, Q_PRIMITIVE_TYPE);
