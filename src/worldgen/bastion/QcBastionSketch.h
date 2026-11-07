// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/bastion/QcBastionPlan.h"

#include <functional>

/*! \brief The size of a doorway: \c {half} cells to each side of its middle, \c {height} cells
    high and through \c {thick} cells of wall. \qcfields */
struct QcBastionDoorway
{
    int half = 1;
    int height = 3;
    int thick = 1;
};

/*! \brief Draws a bastion into its plan (decision E-1087): boxes, halls, stairs and landings in
    cells around the plan's middle, x and z from it and y from its floor, turned by quarter turns
    into the world. A box's corners may come in any order. */
class QcBastionSketch
{
public:
    /*! The draws of a sketch, each from 0 to 1. */
    using Roll = std::function<double()>;
    /*! Answers whether the rock stands at a world cell. */
    using IsRock = std::function<bool(QcBlockPos)>;
    /*! The sides of the plan. */
    using Side = QcBastionPlan::Side;
    /*! The paints of the plan. */
    using Paint = QcBastionPlan::Paint;
    /*! The loot tables of the plan. */
    using Loot = QcBastionPlan::Loot;
    /*! The residents of the plan. */
    using Folk = QcBastionPlan::Folk;

    /*! Draws into \a plan, which outlives it and has its middle set, turned \a turns quarter turns
        clockwise, with the draws of \a roll; \a isRock tells where a landing can end. */
    QcBastionSketch(QcBastionPlan &plan, int turns, Roll roll, IsRock isRock);

    /*! Returns a draw from 0 to 1. */
    [[nodiscard]] double roll() const { return m_roll(); }

    /*! Paints \a box with \a paint; a wall or stairs take \a side. */
    void paint(const QcBlockBox &box, Paint paint, Side side = Side::North);
    /*! Notes \a box as a part of the bastion, for its spawn area. */
    void addPart(const QcBlockBox &box);
    /*! Stands a hall in \a box on a foundation: floor, walls and roof of bricks, empty inside. */
    void hall(const QcBlockBox &box);
    /*! Stands a yard in \a box on a foundation: a floor of rough stone at its low y and a curtain
        wall \a thick cells thick up to its high y, open above. */
    void yard(const QcBlockBox &box, int thick);
    /*! Lays a deck of bricks over \a box's columns at its low y, with a wall along both sides of
        the axis of \a along and a free space over it up to its high y. */
    void deck(const QcBlockBox &box, Side along);
    /*! Cuts a doorway of \a size from the wall cell \a at, its lowest in the middle, inward
        against \a outward. */
    void door(QcBlockPos at, Side outward, QcBastionDoorway size = {});
    /*! Lays \a rise steps \a width cells wide from the floor cell \a from towards \a side, the
        width going clockwise from it, on columns of brick and with room over them. */
    void flight(QcBlockPos from, Side side, int rise, int width);
    /*! Lays a landing from the outer side of the doorway at the floor cell \a at towards \a side:
        a jetty on piers through a free space, up to the first ground, or the longest it may be. */
    void landing(QcBlockPos at, Side side);
    /*! Stands merlons around the top of \a box, on every other cell. */
    void merlons(const QcBlockBox &box);
    /*! Lets the cells of \a box fall away towards its top. */
    void ruin(const QcBlockBox &box);
    /*! Stands a chest at \a at with its front to \a facing, filled from \a loot. */
    void chest(QcBlockPos at, Side facing, Loot loot);
    /*! Stands a spawner at \a at. */
    void spawner(QcBlockPos at);
    /*! Stands one of \a folk with its feet at \a at. */
    void resident(QcBlockPos at, Folk folk);
    /*! Puts the sign \a text at \a at, \a heading for the name of the whole; only a demonstration
        world shows it. */
    void sign(QcBlockPos at, const QString &text, bool heading = false);
    /*! Ends the sketch: sets the plan's bounds. */
    void finish();

private:
    [[nodiscard]] QcBastionPlan &plan() { return m_plan; }
    // Whether the world cell `at` is rock.
    [[nodiscard]] bool isRock(QcBlockPos at) const { return m_isRock(at); }
    // The world cell of the cell `local`.
    [[nodiscard]] QcBlockPos world(QcBlockPos local) const;
    // The world box of `box`.
    [[nodiscard]] QcBlockBox world(const QcBlockBox &box) const;
    // The world side of `side`.
    [[nodiscard]] Side world(Side side) const;

    QcBastionPlan &m_plan;
    int m_turns = 0;
    Roll m_roll;
    IsRock m_isRock;
};
