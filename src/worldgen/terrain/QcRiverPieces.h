// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/terrain/QcRiverField.h"

#include <QtGlobal>

#include <array>
#include <optional>

// Whether a river leads anywhere (decision E-851): the middle line of a river is followed both
// ways from a point on it until it reaches the sea, runs dry (a desert, the fading ring of a small
// hill of the noise) or comes back round to the point. A piece that ends on land at both ends or
// runs round in a loop, all of it within `longest` blocks, is closed: the terrain does not build
// it. Longer pieces count as open, whichever point they are followed from, so every column of a
// piece gets the same answer. Deterministic per seed and thread-safe; the answers are remembered
// per thread by cells of 32 blocks along the rivers.
class QcRiverPieces
{
public:
    // A piece that ends on land at both ends within this many blocks along its middle line is
    // closed (the window of the river tests is 3072 blocks wide).
    static constexpr double longest = 2400.0;

    // `rivers` must outlive the pieces (the terrain generator owns both).
    explicit QcRiverPieces(const QcRiverField &rivers);

    // Whether the river of `sample` (a column within its reach, QcRiverField::at()) reaches the
    // sea or runs on for more than `longest` blocks.
    [[nodiscard]] bool leadsOn(const QcRiverField::Sample &sample, const QcRiverField::Land &land) const;
    // The answers are found from the middle of cells of this many blocks (bent coordinates; a
    // column of a river finds the cell of the nearest point of its middle line, Sample::lineX/lineZ).
    static constexpr double cellSize = 32.0;
    // The answer for the cell (`cellX`, `cellZ`) of cellSize blocks, remembered per thread.
    [[nodiscard]] bool cellLeadsOn(qint32 cellX, qint32 cellZ, const QcRiverField::Land &land) const;

    // How a walk along the middle line ended: in the sea, where the river runs dry, back at its
    // start, after the walk's length, or off the line (the steps did not settle on it).
    enum class End : quint8 { Sea, Dry, Round, Far, Lost };
    struct Walk
    {
        End end = End::Far;
        double length = 0.0;
    };
    // The walk the answer of a point near a middle line (bent coordinates) reads way `side` (1 or
    // -1 times the line's direction there), started as for a cell (at the first crossing ahead) and
    // given `longest` blocks; none where no walk starts (the river stays). For tests (E-935).
    [[nodiscard]] std::optional<Walk> walkFrom(QcRiverField::Point near, double side, const QcRiverField::Land &land) const;

private:
    // The middle line's point near `near` (bent coordinates, at most `reach` blocks away),
    // with the unit direction along the line (the noise's gradient turned by a right angle); none
    // where the steps do not settle.
    struct OnLine
    {
        QcRiverField::Point at;
        double alongX = 0.0;
        double alongZ = 0.0;
    };
    [[nodiscard]] std::optional<OnLine> ontoLine(QcRiverField::Point near, double reach) const;
    // The direction along the middle line at its point `at` that keeps on towards (towardsX,
    // towardsZ).
    [[nodiscard]] OnLine along(QcRiverField::Point at, double towardsX, double towardsZ) const;
    // Where the middle line crosses lines of the lattice between two of its points a march step
    // apart, in the order from `from` to `to` (one that does not settle is left out, and so is one
    // at or behind `from`: it was passed before).
    struct Crossings
    {
        std::array<QcRiverField::Point, 2> at;
        int count = 0;
    };
    [[nodiscard]] Crossings crossings(QcRiverField::Point from, QcRiverField::Point to) const;
    // The middle line's crossing with the lattice line x = `line` (`acrossX`) or z = `line` near
    // `guess`.
    [[nodiscard]] std::optional<QcRiverField::Point> crossingOn(bool acrossX, double line, QcRiverField::Point guess) const;
    // A crossing a walk passed: where, and how strongly the climate lets rivers run there
    // (QcRiverField::strengthAt(), 0 in the sea).
    struct Passed
    {
        QcRiverField::Point at;
        double strength = 0.0;
    };
    // What a walk sees at a crossing (bent coordinates): whether the river ends there (in the sea
    // or where it runs dry) and the crossing with its climate's strength.
    struct Seen
    {
        End end = End::Far;
        Passed passed;
    };
    [[nodiscard]] Seen seenAt(QcRiverField::Point at, const QcRiverField::Land &land) const;
    // Where the river runs dry on its middle line between two crossings passed one after the
    // other, if it does: the same points are read whichever way a walk passes them.
    [[nodiscard]] std::optional<QcRiverField::Point> dryBetween(const Passed &one, const Passed &other) const;
    // The crossings a walk has passed so far: its first one (back there, it went round a loop) and
    // the last one.
    struct Trail
    {
        std::optional<QcRiverField::Point> first;
        std::optional<Passed> last;
    };
    // What passing a crossing `length` blocks along a walk gives: the walk's end there, or the
    // crossing passed (neither where it is the last one found again).
    struct Passing
    {
        std::optional<Walk> ended;
        std::optional<Passed> passed;
    };
    [[nodiscard]] Passing pass(const Trail &trail, QcRiverField::Point at, double length, const QcRiverField::Land &land) const;
    // Walks along the middle line from `from` (in its direction) for at most `budget` blocks, having
    // passed `trail` already.
    [[nodiscard]] Walk walk(const OnLine &from, double budget, Trail trail, const QcRiverField::Land &land) const;
    // Where both walks of a point near a middle line start: the first crossing ahead of the
    // point's point of the line (direction as there) and what is seen at it; none off the line.
    struct Start
    {
        OnLine at;
        Seen seen;
    };
    [[nodiscard]] std::optional<Start> startNear(QcRiverField::Point near, const QcRiverField::Land &land) const;
    // The walk from `start` way `side` for at most `budget` blocks.
    [[nodiscard]] Walk walkFrom(const Start &start, double side, double budget, const QcRiverField::Land &land) const;
    // The answer followed from a point near a middle line.
    [[nodiscard]] bool follow(QcRiverField::Point from, const QcRiverField::Land &land) const;

    [[nodiscard]] static quint64 nextInstance();

    quint64 m_instance = 0;
    const QcRiverField &m_rivers;
};
