// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTerrainClimate.h"

#include <QtGlobal>

#include <optional>

/*! \brief The rivers of the terrain (decision E-840): winding channels along the zero line of their
    own noise field, bent a second time so they meander.

    Their water stands at the sea level, so a river is one body of water with the sea and the
    lakes it reaches; the channel and the valley around it are carved into the land, never
    raised. Through rugged land (mountain ranges) a river runs in a narrow gorge; in dry land
    (deserts) and where the zero line curves tightly (rings round a hill of the noise) it narrows
    and ends. Deterministic per seed and thread-safe. */
class QcRiverField
{
public:
    /*! Distance of a column far from every river. */
    static constexpr double far = 1.0e9;
    /*! Beyond this many blocks from its middle line a river changes nothing. */
    static constexpr double reach = 44.0;
    /*! A river never changes ground deeper than this under the sea level (its deepest bed, 1.2 +
        0.45 * 7.5 blocks, plus the smooth seam of the carving). */
    static constexpr double reachDown = 8.0;
    /*! Half the width of the narrowest full river (before gorges, dry land and ends narrow it
        further). */
    static constexpr double narrowestHalfWidth = 2.5;
    /*! The deepest bed a river carves under the sea level. */
    static constexpr double deepestBed = 1.2 + 0.45 * 7.5;

    /*! \brief The terrain's maps that the river pieces (QcRiverPieces) and deltas (QcDeltaSites)
        read (QcTerrainGenerator). */
    class Land
    {
    public:
        virtual ~Land() = default;
        /*! Returns the continent value at the column \a x, \a z (below about -0.15 the sea). */
        [[nodiscard]] virtual double continentAt(int x, int z) const = 0;
        /*! Returns the climate point of the column \a x, \a z. */
        [[nodiscard]] virtual QcTerrainClimate::Point climateAt(int x, int z) const = 0;
        /*! Returns the climate at the column \a x, \a z if its ground (before rivers cut it)
            lies above the sea level or at most a block under it; std::nullopt in the sea, lakes
            and other ground deeper under it. */
        [[nodiscard]] virtual std::optional<QcTerrainClimate::Point> dryClimateAt(int x, int z) const = 0;
    };

    /*! \brief The river near one column. */
    struct Sample
    {
        /*! Blocks from the middle line of the nearest river, about (the noise over its slope);
            \c {far} away from rivers. */
        double distance = far;
        /*! Half the width of the water at this point of the river, 0 where none runs. */
        double halfWidth = 0.0;
        /*! Depth of the water at the middle line below the water surface. */
        double depth = 0.0;
        /*! How much of the valley is carved (0..1): 1 along the river, falling to 0 where the
            river ends in dry land or at a tight bend of its zero line. */
        double strength = 0.0;
        /*! How much steeper than usual the banks rise: 1 in low land, more in rugged land, where
            the river runs through a gorge. */
        double steepness = 1.0;
        /*! The river noise at the column (its zero line is the middle line of the rivers),
            wherever the river field was asked and the land is not dry; \c {far} elsewhere. The
            river deltas (decision E-850) read it to skip columns far from every river at once. */
        double value = far;
        /*! The point of the middle line nearest the column in coordinates bent by the meander
            (one step along the noise's slope), where \c {distance} is below \c {reach}.
            \qcshared */
        double lineX = 0.0;
        double lineZ = 0.0;

        /*! Returns whether the column lies in the water of the channel (before the terrain's
            rounding). */
        [[nodiscard]] bool inChannel() const { return distance < halfWidth; }
    };

    explicit QcRiverField(quint64 seed);

    /*! Returns the river near the column \a x, \a z with the climate point \a climate. */
    [[nodiscard]] Sample at(int x, int z, const QcTerrainClimate::Point &climate) const;
    /*! \brief A column of land height \c {height} carved by the river near it: the new height
        (unrounded), whether the river put water there (not the sea or a lake it runs into), whether
        it is a bank (dry, low, right beside the water) and the river asked.

        Ground more than 8 blocks under the sea level (the sea, deep lakes) asks no river and
        stays as it is. \qcfields */
    struct Cut
    {
        double height = 0.0;
        bool water = false;
        bool bank = false;
        Sample river;
    };
    /*! Returns the column \a x, \a z with the climate point \a climate and the land height
        \a height carved by the river near it, water and bank judged against \a seaLevel. */
    [[nodiscard]] Cut cut(int x, int z, const QcTerrainClimate::Point &climate, double height, int seaLevel) const;
    /*! Returns the surface height (first air above the ground, unrounded) of a column of height
        \a height after the river at \a sample carved it: the channel down to \c {depth} below
        \a seaLevel, the banks rising from the water's edge. Never higher than \a height. */
    [[nodiscard]] static double carve(double height, const Sample &sample, int seaLevel);
    /*! Returns a value in [0, 1) at the column \a x, \a z that is the same over patches of a few
        blocks: which block of a river bed or bank lies in a column. */
    [[nodiscard]] double patchAt(int x, int z) const;

    /*! Returns the river noise at the column \a x, \a z (bent by the meander, as at() reads
        it). */
    [[nodiscard]] double valueAt(int x, int z) const;
    /*! \brief Where a column lies across the river near it (decision E-850), from the river noise
        \c {here} at the column: its signed distance from the middle line in blocks (the noise over
        its slope; the side the noise is positive on counts positive), the unit direction across the
        river (the noise's gradient in real coordinates, the meander included) and how wide the
        river runs there (0..1, its share between the narrowest and the widest river, before gorges
        or dry land narrow it).

        Costs two more readings of the noise. \qcfields */
    struct Course
    {
        double offset = far;
        double acrossX = 1.0;
        double acrossZ = 0.0;
        double width = 0.0;
    };
    /*! Returns where the column \a x, \a z lies across the river near it, from the river noise
        \a here at the column (valueAt()); the defaults where the noise has no slope. */
    [[nodiscard]] Course courseAt(int x, int z, double here) const;
    /*! Returns how strongly rivers run at the point \a climate (0..1, by its humidity): none in
        the driest deserts. */
    [[nodiscard]] static double strengthAt(const QcTerrainClimate::Point &climate);

    /*! Returns the river noise at the point \a bentX, \a bentZ in coordinates bent by the
        meander (its zero line is the middle line). value(), unbent() and ringsLeaveOnLine() read
        the middle lines in bent coordinates, for following a river along them (QcRiverPieces). */
    [[nodiscard]] double value(double bentX, double bentZ) const;
    /*! \brief An x and z pair in blocks: a point in real or bent coordinates (as its user says),
        or the meander's bend at a column. */
    struct Point
    {
        /*! The x and z coordinate. \qcshared */
        double x = 0.0;
        double z = 0.0;
    };
    /*! Returns the column whose bent coordinates are \a bentX, \a bentZ (within about a quarter
        block). */
    [[nodiscard]] Point unbent(double bentX, double bentZ) const;
    /*! Returns how much of the river the fading of small rings leaves on its middle line at the
        bent point \a bentX, \a bentZ (0..1; times the climate's strengthAt() it is how strongly
        the river runs there, as at()). */
    [[nodiscard]] double ringsLeaveOnLine(double bentX, double bentZ) const;

private:
    // The meander's bend at a column: how many blocks its coordinates move.
    [[nodiscard]] Point bendAt(double x, double z) const;
    // The river noise at a point: its value, gradient and Hessian (differences over `step` blocks).
    struct Local
    {
        double value = 0.0;
        double gradX = 0.0;
        double gradZ = 0.0;
        double xx = 0.0;
        double zz = 0.0;
        double xz = 0.0;
    };
    [[nodiscard]] Local localAt(double bentX, double bentZ, double here, double step) const;
    // The river noise around a column at bent coordinates where it has the value `here`: its
    // gradient and slope per block, the radius `top` in blocks round which its zero line bends near the column (`far`
    // where it runs straight), where to search for the top a ring would have (towards the centre
    // of the bend) and whether that top is a hill (side 1) or a hollow (side -1).
    struct Shape
    {
        double gradX = 0.0;
        double gradZ = 0.0;
        double slope = 0.0;
        double top = far;
        double topX = 0.0;
        double topZ = 0.0;
        double side = 1.0;
    };
    [[nodiscard]] Shape shapeAt(double bentX, double bentZ, double here) const;
    // How much of a river is left (0..1) by the rings of the zero line near a point at bent
    // coordinates of the shape `shape` (1 where it bends round no small hill or hollow).
    [[nodiscard]] double ringsLeave(const Shape &shape) const;
    // How much of a river is left (0..1) by the ring its zero line forms round the top of the hill
    // (side 1) or hollow (side -1) of the noise searched from (startX, startZ): 0 for a small ring,
    // 1 for a large one or none (no top found within a few steps).
    [[nodiscard]] double ringFade(double startX, double startZ, double side) const;
    // ringFade() without remembering: the search itself.
    [[nodiscard]] double searchRing(double startX, double startZ, double side) const;
    // Which search ringFade() remembers: the field (each instance its own), the cell of the start
    // and the side.
    struct TopKey
    {
        quint64 field = 0;
        int cellX = 0;
        int cellZ = 0;
        bool hill = true;

        bool operator==(const TopKey &) const = default;
    };
    struct TopKeyHash
    {
        std::size_t operator()(const TopKey &key) const;
    };
    // The top of a hill (side 1) or hollow (side -1) of the noise as a paraboloid from `local`: the
    // step there and the longer half axis of the zero line's ellipse round it; std::nullopt where
    // the noise does not curve round such a top there.
    struct Top
    {
        double x = 0.0;
        double z = 0.0;
        double value = 0.0;
        double halfAxis = 0.0;
        // The direction of the longer half axis (a unit vector).
        double axisX = 1.0;
        double axisZ = 0.0;
    };
    [[nodiscard]] static std::optional<Top> topOf(const Local &local, double side);
    // How surely (0..1) the noise round `top` at (topX, topZ) is a long hill (or hollow) that goes
    // on past the ellipse, not a ring.
    [[nodiscard]] double longHill(double topX, double topZ, const Top &top) const;
    // The share of the width noise at a column (0..1).
    [[nodiscard]] double widthAt(int x, int z) const;

    // A number of its own for each field, so a new field with the same seed (a test, a measurement)
    // searches anew.
    [[nodiscard]] static quint64 nextInstance();

    quint64 m_instance = 0;
    QcNoise m_riverNoise;
    QcNoise m_meanderNoiseX;
    QcNoise m_meanderNoiseZ;
    QcNoise m_widthNoise;
    QcNoise m_patchNoise;
};
