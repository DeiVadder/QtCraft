#pragma once

#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTerrainClimate.h"

#include <QtGlobal>

#include <optional>

// The rivers of the terrain (decision E-840): winding channels along the zero line of their own
// noise field, bent a second time so they meander. Their water stands at the sea level, so a river
// is one body of water with the sea and the lakes it reaches; the channel and the valley around it
// are carved into the land, never raised. Through rugged land (mountain ranges) a river runs in a
// narrow gorge; in dry land (deserts) and where the zero line curves tightly (rings round a hill of
// the noise) it narrows and ends. Deterministic per seed and thread-safe.
class QcRiverField
{
public:
    // Distance of a column far from every river.
    static constexpr double far = 1.0e9;
    // Beyond this many blocks from its middle line a river changes nothing.
    static constexpr double reach = 44.0;

    // The river near one column.
    struct Sample
    {
        // Blocks from the middle line of the nearest river, about (the noise over its slope); `far`
        // away from rivers.
        double distance = far;
        // Half the width of the water at this point of the river, 0 where none runs.
        double halfWidth = 0.0;
        // Depth of the water at the middle line below the water surface.
        double depth = 0.0;
        // How much of the valley is carved (0..1): 1 along the river, falling to 0 where the river
        // ends in dry land or at a tight bend of its zero line.
        double strength = 0.0;
        // How much steeper than usual the banks rise: 1 in low land, more in rugged land, where the
        // river runs through a gorge.
        double steepness = 1.0;

        // Whether the column lies in the water of the channel (before the terrain's rounding).
        [[nodiscard]] bool inChannel() const { return distance < halfWidth; }
    };

    explicit QcRiverField(quint64 seed);

    // The river near a column with the climate point `climate`.
    [[nodiscard]] Sample at(int x, int z, const QcTerrainClimate::Point &climate) const;
    // A column of land height `height` carved by the river near it: the new height (unrounded),
    // whether the river put water there (not the sea or a lake it runs into), whether it is a bank
    // (dry, low, right beside the water) and the river asked. Ground more than 8 blocks under the sea
    // level (the sea, deep lakes) asks no river and stays as it is.
    struct Cut
    {
        double height = 0.0;
        bool water = false;
        bool bank = false;
        Sample river;
    };
    [[nodiscard]] Cut cut(int x, int z, const QcTerrainClimate::Point &climate, double height, int seaLevel) const;
    // The surface height (first air above the ground, unrounded) of a column of height `height`
    // after the river at `sample` carved it: the channel down to `depth` below the sea level, the
    // banks rising from the water's edge. Never higher than `height`.
    [[nodiscard]] static double carve(double height, const Sample &sample, int seaLevel);
    // A value in [0, 1) that is the same over patches of a few blocks: which block of a river bed
    // or bank lies in a column.
    [[nodiscard]] double patchAt(int x, int z) const;

private:
    // The river noise at coordinates bent by the meander: its zero line is the middle line of the
    // rivers.
    [[nodiscard]] double value(double bentX, double bentZ) const;
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
    // The river noise around a column at bent coordinates where it has the value `here`: its slope
    // per block, the radius `top` in blocks round which its zero line bends near the column (`far`
    // where it runs straight), where to search for the top a ring would have (towards the centre
    // of the bend) and whether that top is a hill (side 1) or a hollow (side -1).
    struct Shape
    {
        double slope = 0.0;
        double top = far;
        double topX = 0.0;
        double topZ = 0.0;
        double side = 1.0;
    };
    [[nodiscard]] Shape shapeAt(double bentX, double bentZ, double here) const;
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
    // How strongly rivers run at a climate point (0..1): none in the driest deserts.
    [[nodiscard]] static double strengthAt(const QcTerrainClimate::Point &climate);

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
