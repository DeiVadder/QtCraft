#include "worldgen/terrain/QcRiverField.h"

#include "worldgen/terrain/QcTerrainMath.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <optional>
#include <unordered_map>

using QcTerrainMath::smoothstep;

namespace {

// Horizontal scale of the river noise: neighbouring rivers lie a few hundred blocks apart.
constexpr double riverScale = 700.0;
// The meander: the river noise is read at coordinates bent by up to this many blocks on this scale.
constexpr double meanderScale = 150.0;
constexpr double meanderAmplitude = 40.0;
// Usual slope of the river noise per block. The distance to the middle line is the noise over its
// slope; where the noise lies above `cutoff` the column is out of `reach` at that slope, and the
// slope is not computed.
constexpr double typicalSlope = 0.0016;
constexpr double cutoff = 1.5 * typicalSlope * QcRiverField::reach;
// Half the width of the water: between these numbers of blocks, on a scale of about widthScale.
constexpr double widthScale = 380.0;
constexpr double narrowest = 2.5;
constexpr double widest = 7.5;
// Depth of the water at the middle line: base plus a share of the half width.
constexpr double baseDepth = 1.2;
constexpr double depthPerWidth = 0.45;
// Banks rise from the water's edge by bankSlope per block plus bankCurve per block squared: gentle
// at the water, steeper further out, so a valley through hills stays a valley.
constexpr double bankSlope = 0.12;
constexpr double bankCurve = 0.02;
// In rugged land (mountain ranges) a river runs through a gorge: its banks rise up to `gorge`
// times as steeply, and the water is up to `gorgeNarrowing` narrower.
constexpr double ruggedFrom = 0.30;
constexpr double ruggedTo = 0.60;
constexpr double gorge = 6.0;
constexpr double gorgeNarrowing = 0.5;
// Rivers fade out in dry land (deserts).
constexpr double dryTo = 0.05;
constexpr double dryFrom = 0.22;
// The valley is carved fully where the strength reaches this.
constexpr double fullValley = 0.3;
// The valley fades out over this many blocks before `reach`.
constexpr double valleyFade = 12.0;
// Patches of the river bed and banks are about this many blocks across.
constexpr double patchScale = 7.0;

// Rings of the zero line round a small hill or hollow of the river noise (whose top lies just past
// zero) fade out; long rivers bending round the end of a long hill or past a saddle stay. Where the
// zero line, without the meander's bend, bends round a radius below `nearTopTo` blocks, the top
// near its centre of curvature is found and the ring round it measured: the river narrows and ends
// where the ring is closed and its longer half axis shorter than ringTo blocks, and is gone below
// ringFrom.
constexpr double nearTopFrom = 160.0;
constexpr double nearTopTo = 240.0;
constexpr double ringFrom = 90.0;
constexpr double ringTo = 140.0;
// The top is searched from the centre of the bend in at most topSearch steps of at most topWalk
// blocks, measuring the noise from differences over topStep blocks.
constexpr int topSearch = 6;
// The search starts in the middle of cells of this many blocks; the results of the last cells
// searched (at most maxRemembered) are kept per thread.
constexpr double topCell = 8.0;
constexpr std::size_t maxRemembered = 1 << 15;
// The ring is closed where the noise this many half axes from the top along its longer axis has
// crossed zero (its value over the top's below closedFrom); a long hill goes on with values above
// closedTo.
constexpr double probeBeyond = 1.5;
constexpr double closedFrom = -0.15;
constexpr double closedTo = 0.15;
constexpr double topWalk = 40.0;
constexpr double topStep = 2.0;

// A river never changes ground deeper than this under the sea level (its deepest bed, 1.2 + 0.45 *
// 7.5 blocks, plus the smooth seam of the carving).
constexpr double reachDown = 8.0;
// Dry columns this many blocks beyond a river's water and at most one block above it are its bank.
constexpr double bankWidth = 3.0;

// Smooth minimum of the land and the valley over about this many blocks of height.
constexpr double blendHeight = 2.0;

// The smaller of a and b with a rounded seam about k wide (polynomial smooth minimum).
double smoothMin(double a, double b, double k)
{
    const double h = std::clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return b + (a - b) * h - k * h * (1.0 - h);
}

} // namespace

quint64 QcRiverField::nextInstance()
{
    static std::atomic<quint64> instances{0};
    return ++instances;
}

std::size_t QcRiverField::TopKeyHash::operator()(const TopKey &key) const
{
    const quint64 cell = (quint64(quint32(key.cellX)) << 32) | quint32(key.cellZ);
    return std::size_t(qcMixSeed(key.field ^ cell, key.hill ? 1 : 2));
}

QcRiverField::QcRiverField(quint64 seed)
    : m_instance(nextInstance())
    , m_riverNoise(qcMixSeed(seed, 40))
    , m_meanderNoiseX(qcMixSeed(seed, 41))
    , m_meanderNoiseZ(qcMixSeed(seed, 42))
    , m_widthNoise(qcMixSeed(seed, 43))
    , m_patchNoise(qcMixSeed(seed, 44))
{
}

double QcRiverField::value(double bentX, double bentZ) const
{
    return m_riverNoise.fractal2(bentX / riverScale, bentZ / riverScale, 2);
}

double QcRiverField::strengthAt(const QcTerrainClimate::Point &climate)
{
    return smoothstep(dryTo, dryFrom, climate.humidity);
}

QcRiverField::Sample QcRiverField::at(int x, int z, const QcTerrainClimate::Point &climate) const
{
    Sample sample;
    sample.strength = strengthAt(climate);
    if (sample.strength <= 0.0) {
        return {};
    }
    const double bentX = x + meanderAmplitude * m_meanderNoiseX.fractal2(x / meanderScale, z / meanderScale, 2);
    const double bentZ = z + meanderAmplitude * m_meanderNoiseZ.fractal2(x / meanderScale, z / meanderScale, 2);
    const double here = value(bentX, bentZ);
    if (std::abs(here) >= cutoff) {
        return {};
    }

    // The distance to the zero line: the value over its slope, at most `reach` at the cutoff so the
    // field stays continuous where the slope is skipped. The slope is taken at the bent coordinates
    // (the meander stretches or squeezes the width by up to about a quarter, which looks natural).
    const Shape shape = shapeAt(bentX, bentZ, here);
    const double capped = reach * std::abs(here) / cutoff;
    sample.distance = shape.slope > 0.0 ? std::max(std::abs(here) / shape.slope, capped) : capped;
    // Only where the river changes anything (within `reach`) the top is searched.
    const double nearTop = smoothstep(nearTopFrom, nearTopTo, shape.top);
    if (nearTop < 1.0 && sample.distance < reach) {
        sample.strength *= std::max(nearTop, ringFade(shape.topX, shape.topZ, shape.side));
    }

    const double width = smoothstep(-0.4, 0.4, m_widthNoise.fractal2(x / widthScale, z / widthScale, 2));
    const double rugged = smoothstep(ruggedFrom, ruggedTo, climate.ruggedness);
    sample.steepness = 1.0 + (gorge - 1.0) * rugged;
    sample.halfWidth = sample.strength * (1.0 - gorgeNarrowing * rugged) * (narrowest + (widest - narrowest) * width);
    sample.depth = baseDepth + depthPerWidth * sample.halfWidth;
    return sample;
}

QcRiverField::Local QcRiverField::localAt(double bentX, double bentZ, double here, double step) const
{
    const double east = value(bentX + step, bentZ);
    const double west = value(bentX - step, bentZ);
    const double south = value(bentX, bentZ + step);
    const double north = value(bentX, bentZ - step);
    const double corner = value(bentX + step, bentZ + step);
    const double square = step * step;
    return {here,
            0.5 * (east - west) / step,
            0.5 * (south - north) / step,
            (east - 2.0 * here + west) / square,
            (south - 2.0 * here + north) / square,
            (corner - east - south + here) / square};
}

QcRiverField::Shape QcRiverField::shapeAt(double bentX, double bentZ, double here) const
{
    const Local local = localAt(bentX, bentZ, here, 1.0);
    Shape shape;
    shape.slope = std::hypot(local.gradX, local.gradZ);
    if (shape.slope <= 0.0) {
        return shape;
    }

    // The line of equal value through the column bends by `curvature` (the divergence of the unit
    // gradient) round its centre of curvature; a ring of the zero line has the top of its hill or
    // hollow there. The zero line lies about parallel, `here / slope` blocks away against the
    // gradient, so its radius differs by that distance.
    const double gx = local.gradX / shape.slope;
    const double gz = local.gradZ / shape.slope;
    const double curvature = (local.xx * gz * gz - 2.0 * local.xz * gx * gz + local.zz * gx * gx) / shape.slope;
    if (curvature == 0.0) {
        return shape;
    }
    shape.top = std::abs(1.0 / curvature - here / shape.slope);
    // Inside a ring round a hill (the gradient pointing in, the line bending round it) the noise
    // lies above zero, inside one round a hollow below. The search for the top starts at most
    // topWalk blocks towards the centre of the bend.
    shape.side = curvature < 0.0 ? 1.0 : -1.0;
    const double towards = std::clamp(1.0 / curvature, -topWalk, topWalk);
    shape.topX = bentX - gx * towards;
    shape.topZ = bentZ - gz * towards;
    return shape;
}

double QcRiverField::ringFade(double startX, double startZ, double side) const
{
    // The search starts in the middle of a cell of topCell blocks: every column whose start lies
    // in the cell finds the same top, so the result is remembered per thread and field (a pure
    // function of the field, cell and side, so the order of generation does not matter).
    const TopKey key{m_instance, int(std::floor(startX / topCell)), int(std::floor(startZ / topCell)), side > 0.0};
    thread_local std::unordered_map<TopKey, double, TopKeyHash> remembered;
    if (const auto found = remembered.find(key); found != remembered.end()) {
        return found->second;
    }
    if (remembered.size() >= maxRemembered) {
        remembered.clear();
    }
    const double fade = searchRing((key.cellX + 0.5) * topCell, (key.cellZ + 0.5) * topCell, side);
    remembered.emplace(key, fade);
    return fade;
}

double QcRiverField::searchRing(double startX, double startZ, double side) const
{
    double x = startX;
    double z = startZ;
    for (int i = 0; i < topSearch; ++i) {
        const Local local = localAt(x, z, value(x, z), topStep);
        const std::optional<Top> top = topOf(local, side);
        if (!top) {
            // Not yet on the top's rounded part: up the hill (down the hollow) along the gradient.
            const double slope = std::hypot(local.gradX, local.gradZ);
            if (slope <= 0.0) {
                return 1.0;
            }
            x += side * topWalk * local.gradX / slope;
            z += side * topWalk * local.gradZ / slope;
            continue;
        }
        const double step = std::hypot(top->x, top->z);
        if (step <= topWalk) {
            return std::max(smoothstep(ringFrom, ringTo, top->halfAxis), longHill(x + top->x, z + top->z, *top));
        }
        x += topWalk * top->x / step;
        z += topWalk * top->z / step;
    }
    return 1.0;
}

std::optional<QcRiverField::Top> QcRiverField::topOf(const Local &local, double side)
{
    // The noise near the point as a paraboloid opening away from `side`: one Newton step to its
    // top, the value there (on `side` of zero), and the zero line round it an ellipse with half
    // axes sqrt(2 |top| / |curvature|) along the axes of the Hessian, the longer one along the
    // flatter axis.
    const double det = local.xx * local.zz - local.xz * local.xz;
    const double mean = 0.5 * (local.xx + local.zz);
    if (det <= 0.0 || mean * side >= 0.0) {
        return std::nullopt;
    }
    Top top;
    top.x = -(local.zz * local.gradX - local.xz * local.gradZ) / det;
    top.z = -(local.xx * local.gradZ - local.xz * local.gradX) / det;
    const double value = local.value + 0.5 * (local.gradX * top.x + local.gradZ * top.z);
    if (value * side <= 0.0) {
        return std::nullopt;
    }
    const double flattest = mean + side * std::hypot(0.5 * (local.xx - local.zz), local.xz);
    top.value = value;
    top.halfAxis = std::sqrt(2.0 * std::abs(value) / std::abs(flattest));
    // The eigenvector (xz, flattest - xx), or the x axis where the Hessian is diagonal along it.
    const bool alongX = local.xz == 0.0 && flattest == local.xx;
    const double axisX = alongX ? 1.0 : local.xz;
    const double axisZ = alongX ? 0.0 : flattest - local.xx;
    const double length = std::hypot(axisX, axisZ);
    top.axisX = axisX / length;
    top.axisZ = axisZ / length;
    return top;
}

double QcRiverField::longHill(double topX, double topZ, const Top &top) const
{
    // Past the ellipse on both ends of its longer axis the noise of a ring has crossed zero; a long
    // hill (a river bending round its end) goes on there on at least one side.
    const double reachOut = probeBeyond * top.halfAxis;
    const double ahead = value(topX + reachOut * top.axisX, topZ + reachOut * top.axisZ) / top.value;
    const double behind = value(topX - reachOut * top.axisX, topZ - reachOut * top.axisZ) / top.value;
    return smoothstep(closedFrom, closedTo, std::max(ahead, behind));
}

QcRiverField::Cut QcRiverField::cut(int x, int z, const QcTerrainClimate::Point &climate, double height, int seaLevel) const
{
    // Ground deeper under water than any river bed (the sea, deep lakes) stays as it is.
    if (height < seaLevel - reachDown) {
        return {height, false, false, {}};
    }
    const Sample river = at(x, z, climate);
    Cut cut{carve(height, river, seaLevel), false, false, river};
    if (river.distance >= reach) {
        return cut;
    }

    // Water the river brought into land (not the sea or a lake it runs into); low dry ground right
    // beside it is its bank.
    const long rounded = std::lround(cut.height);
    cut.water = rounded < seaLevel && height >= seaLevel - 0.5;
    cut.bank = !cut.water && rounded <= seaLevel + 1 && river.distance < river.halfWidth + bankWidth;
    return cut;
}

double QcRiverField::carve(double height, const Sample &sample, int seaLevel)
{
    if (sample.distance >= reach || sample.strength <= 0.0) {
        return height;
    }

    // The water's edge lies where the profile crosses half a block under the sea level: columns
    // inside round down to water, columns outside up to dry ground flush with the water.
    const double edge = seaLevel - 0.5;
    double profile = 0.0;
    if (sample.distance < sample.halfWidth) {
        const double across = sample.distance / sample.halfWidth;
        profile = edge - sample.depth * (1.0 - across * across);
    } else {
        const double out = sample.distance - sample.halfWidth;
        profile = edge + sample.steepness * (bankSlope * out + bankCurve * out * out);
    }

    const double fade = smoothstep(0.0, fullValley, sample.strength) * (1.0 - smoothstep(reach - valleyFade, reach, sample.distance));
    const double carved = std::min(height, smoothMin(height, profile, blendHeight));
    return height - fade * (height - carved);
}

double QcRiverField::patchAt(int x, int z) const
{
    // The noise lies mostly within +-0.5: stretched and clamped it covers [0, 1) about evenly.
    return std::clamp(0.5 + 1.1 * m_patchNoise.fractal2(x / patchScale, z / patchScale, 2), 0.0, 0.999);
}
