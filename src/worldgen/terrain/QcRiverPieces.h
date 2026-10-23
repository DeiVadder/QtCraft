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

private:
    // How a walk along the middle line ended: in the sea, where the river runs dry, back at its
    // start, after the walk's length, or off the line (the steps did not settle on it).
    enum class End : quint8 { Sea, Dry, Round, Far, Lost };
    struct Walk
    {
        End end = End::Far;
        double length = 0.0;
    };
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
    // apart, in the order from `from` to `to` (one that does not settle is left out).
    struct Crossings
    {
        std::array<QcRiverField::Point, 2> at;
        int count = 0;
    };
    [[nodiscard]] Crossings crossings(QcRiverField::Point from, QcRiverField::Point to) const;
    // The middle line's crossing with the lattice line x = `line` (`acrossX`) or z = `line` near
    // `guess`.
    [[nodiscard]] std::optional<QcRiverField::Point> crossingOn(bool acrossX, double line, QcRiverField::Point guess) const;
    // Walks along the middle line from `from` (its direction times `side`, 1 or -1) for at most
    // `budget` blocks.
    [[nodiscard]] Walk walk(const OnLine &from, double side, double budget, const QcRiverField::Land &land) const;
    // Whether the river at a point of its middle line (bent coordinates) ends there: in the sea or
    // where it runs dry.
    [[nodiscard]] End endAt(QcRiverField::Point at, const QcRiverField::Land &land) const;
    // The answer for the middle of a cell, remembered per thread.
    [[nodiscard]] bool cellLeadsOn(qint32 cellX, qint32 cellZ, const QcRiverField::Land &land) const;
    // The answer followed from a point near a middle line.
    [[nodiscard]] bool follow(QcRiverField::Point from, const QcRiverField::Land &land) const;

    [[nodiscard]] static quint64 nextInstance();

    quint64 m_instance = 0;
    const QcRiverField &m_rivers;
};
