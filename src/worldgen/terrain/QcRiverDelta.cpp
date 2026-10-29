// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/terrain/QcRiverDelta.h"

#include "worldgen/terrain/QcDeltaArms.h"
#include "worldgen/terrain/QcTerrainMath.h"

#include <algorithm>
#include <cmath>

using QcTerrainMath::smoothstep;

namespace {

// The band along the coasts where deltas may lie, in continent values (below about -0.15 lies the
// sea); a delta's islands blend out towards its edges.
constexpr double bandLand = 0.18;
constexpr double bandLandFull = 0.12;
constexpr double bandSea = -0.32;
constexpr double bandSeaFull = -0.28;
// Along the fan 0 is the apex, 1 the mouth. Beyond the coast (by each column's own distance to
// it) the banks sink from sinkFrom on and are gone at sinkTo; the fan ends at fanEnd.
constexpr double sinkFrom = 0.95;
constexpr double sinkTo = 1.2;
constexpr double fanEndFrom = 1.25;
constexpr double fanEnd = 1.4;
// Where the coast runs at a slant, a side of the fan reaches further down the river than the mouth:
// up to alongEnd times the fan's length.
constexpr double alongFade = 2.2;
constexpr double alongEnd = 2.5;
// The delta proper (its biome) ends out at sea between these.
constexpr double coreFrom = 1.05;
constexpr double coreTo = 1.2;
// The fan blends in over this much of its length behind the apex.
constexpr double apexBlend = 0.3;

// Half the width of the fan at the coast in blocks, by half the width of the river.
constexpr double fanBase = 12.0;
constexpr double fanPerWidth = 7.0;
constexpr double wideFan = fanBase + fanPerWidth * 7.5;
// The fan's islands reach this share of its half width; beyond, the land rises by basinSlope per
// block over a margin of `margin` blocks (blending back into the land's own height over its outer
// half), so the delta lies in a low basin without cliffs at its sides.
constexpr double islandSide = 0.92;
constexpr double margin = 32.0;
constexpr double basinSlope = 0.2;
// The delta's biome reaches this many blocks beyond the fan's side, and only on ground less than
// dryFrom..dryTo above the sea (higher ground keeps the land's biome and its trees).
constexpr double coreSide = 8.0;
constexpr double dryFrom = 1.6;
constexpr double dryTo = 2.8;
// The fan's frame is bent across by up to `warp` blocks on warpScale and `sway` blocks on
// swayScale: no straight edges, the fan's sides wander in and out.
constexpr double warpScale = 28.0;
constexpr double warp = 4.0;
constexpr double swayScale = 75.0;
constexpr double sway = 7.0;
// The river noise rises by at most about this much per block (twice the usual slope): columns
// where it lies further from zero than the widest fan with its margin (and its bends) are
// skipped at once.
constexpr double steepestSlope = 0.0032;
constexpr double reachValue = steepestSlope * (wideFan * 1.25 * islandSide + margin + sway + warp);

// Islands: about this high over the sea level (half a block rounds to ground flush with the water,
// sand banks; one block and more to mud islands), bumpy on islandScale, higher towards the apex.
constexpr double islandScale = 22.0;
constexpr double islandBase = 0.9;
constexpr double islandBumps = 0.8;
constexpr double islandInland = 0.6;
// How far the banks sink under the sea at the fan's end.
constexpr double sunk = 3.5;
// Under the sea near the coast the delta raises the sea floor by up to `shoal` blocks (sand bars),
// never more: it follows the coast instead of reaching out into the sea.
constexpr double shoal = 1.6;
// Towards the coast the land sinks to the water over a gentle shore: at most shoreSlope blocks per
// block inland above the sea (no cliffs at the coast beside a delta).
constexpr double shoreSlope = 0.12;
constexpr double shoreBase = 0.4;
// Ponds on the islands, about pondScale blocks across, a block deep.
constexpr double pondScale = 9.0;
constexpr double pondFrom = 0.3;
constexpr double pondTo = 0.4;
constexpr double pondDepth = 2.2;
// Smooth minimum of islands and arms over about this much height.
constexpr double seam = 0.6;
// The islands blend in between these weights of the delta, the arms up to armsTo.
constexpr double islandsFrom = 0.15;
constexpr double islandsTo = 0.6;
constexpr double armsTo = 0.35;
constexpr double armsAllowed = 0.15;

double smoothMin(double a, double b, double k)
{
    if (k <= 0.0) {
        return std::min(a, b);
    }
    const double h = std::clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return b + (a - b) * h - k * h * (1.0 - h);
}

// How far the fan has opened along it: from the apex (0) to the coast (1), a little wider out at sea.
double opening(double along)
{
    return along < 1.0 ? std::pow(along, 0.75) : 1.0 + 0.25 * (along - 1.0);
}

} // namespace

QcRiverDelta::QcRiverDelta(quint64 seed, const QcRiverField &rivers, int seaLevel, double share)
    : m_sites(seed, rivers, share)
    , m_seaLevel(seaLevel)
    , m_wiggleNoise(qcMixSeed(seed, 51))
    , m_islandNoise(qcMixSeed(seed, 52))
    , m_pondNoise(qcMixSeed(seed, 53))
{
}

bool QcRiverDelta::mayReach(double continent, double pad)
{
    return continent > bandSea - pad && continent < bandLand + pad;
}

bool QcRiverDelta::hasSiteAt(int x, int z, const Land &land) const
{
    // The same reading as shape() for the column.
    const std::shared_ptr<const QcDeltaSites::Site> site = m_sites.siteNear(x, z, land);
    return site && site->allowed > 0.0;
}

std::optional<QcRiverDelta::Fan> QcRiverDelta::fanAt(int x, int z, double continent, double riverValue) const
{
    // Free tests only: the band, and the river's noise where the river field was asked (far from
    // every river nothing more is read). The rest is the delta of the column's cell (remembered).
    const double allowed = smoothstep(bandSea, bandSeaFull, continent) * (1.0 - smoothstep(bandLandFull, bandLand, continent));
    if (allowed <= 0.0 || (riverValue < QcRiverField::far && std::abs(riverValue) >= reachValue)) {
        return std::nullopt;
    }
    return Fan{x, z, continent, allowed};
}

std::optional<QcRiverDelta::Place> QcRiverDelta::placeIn(const Fan &fan, const QcDeltaSites::Site &site) const
{
    // Along the fan by the straight line from the apex to the mouth, across it from that line (bent
    // a little, so no edge runs straight). Towards the sea by the column's own distance to the
    // coast: the islands sink and the arms run on until the sea, also where the coast runs at a
    // slant.
    const double fromX = fan.x - site.mouthX;
    const double fromZ = fan.z - site.mouthZ;
    Place place;
    place.along = 1.0 + (fromX * site.downX + fromZ * site.downZ) / site.length;
    place.coast = 1.0 - (fan.continent - QcDeltaSites::coastContinent) / site.slope / site.length;
    place.inland = (1.0 - place.coast) * site.length;
    place.halfWidth = (fanBase + fanPerWidth * site.riverHalfWidth) * opening(place.along);
    if (place.along <= 0.0 || place.along >= alongEnd || place.coast >= fanEnd) {
        return std::nullopt;
    }
    place.lateral = fromZ * site.downX - fromX * site.downZ + sway * m_wiggleNoise.fractal2(fan.x / swayScale + 91.7, fan.z / swayScale, 1);
    place.outside = std::max(0.0, std::abs(place.lateral) - islandSide * place.halfWidth);
    if (place.outside >= margin) {
        return std::nullopt;
    }
    place.lateral += warp * m_wiggleNoise.fractal2(fan.x / warpScale, fan.z / warpScale, 2);
    return place;
}

QcRiverDelta::Shape QcRiverDelta::shape(const Fan &fan, const Land &land, Ground own) const
{
    const std::shared_ptr<const QcDeltaSites::Site> site = m_sites.siteNear(fan.x, fan.z, land);
    const std::optional<Place> place = site ? placeIn(fan, *site) : std::nullopt;
    if (!place) {
        return {};
    }

    const double lengthShape = smoothstep(0.0, apexBlend, place->along) * (1.0 - smoothstep(alongFade, alongEnd, place->along))
                               * (1.0 - smoothstep(fanEndFrom, fanEnd, place->coast));
    const double fanShape = lengthShape * (1.0 - smoothstep(0.5 * margin, margin, place->outside));
    Shape shape;
    shape.weight = fan.allowed * site->allowed * fanShape;
    if (shape.weight <= 0.0) {
        return shape;
    }
    // The land blends in over the fan's edges and where the mouth allows less of a delta; the arms
    // lie inside the fan and run on from the apex into the sea.
    const double ground = own.height + smoothstep(islandsFrom, islandsTo, shape.weight) * (landOf(fan, *place, own.height) - own.height);
    const double channel = QcDeltaArms(*site).channelAt(place->along, place->lateral, place->halfWidth, m_seaLevel - 0.5);
    const double arms = own.height + smoothstep(0.0, armsTo, lengthShape) * smoothstep(0.0, armsAllowed, site->allowed) * (channel - own.height);
    shape.height = smoothMin(ground, arms, seam * shape.weight);
    // A river's own channel stays open where it runs off the fan's arms.
    shape.height = own.river ? std::min(shape.height, own.height) : shape.height;
    // The delta's biome on its low land and water near the fan; higher ground keeps the land's.
    shape.core = shape.weight * (1.0 - smoothstep(coreFrom, coreTo, place->coast)) * (1.0 - smoothstep(0.0, coreSide, place->outside))
                 * (1.0 - smoothstep(dryFrom, dryTo, shape.height - m_seaLevel));
    return shape;
}

double QcRiverDelta::landOf(const Fan &fan, const Place &place, double own) const
{
    const double open = std::min(place.along, 1.0);
    double land = m_seaLevel + islandBase + islandBumps * m_islandNoise.fractal2(fan.x / islandScale, fan.z / islandScale, 2)
                  + islandInland * (1.0 - open) + basinSlope * place.outside - sunk * smoothstep(sinkFrom, sinkTo, place.coast);
    // Ponds on the islands (inside the fan only).
    if (place.outside < 4.0) {
        land -= pondDepth * smoothstep(pondFrom, pondTo, m_pondNoise.fractal2(fan.x / pondScale, fan.z / pondScale, 2))
                * (1.0 - smoothstep(0.85, 1.0, place.coast)) * (1.0 - smoothstep(0.0, 4.0, place.outside));
    }
    land = std::min(land, m_seaLevel + shoreBase + shoreSlope * std::max(0.0, place.inland));
    // Never above the land's own ground; under the sea near the coast a little above the floor.
    const double rise = shoal * (1.0 - smoothstep(m_seaLevel - 1.5, m_seaLevel + 0.5, own)) * (1.0 - smoothstep(sinkFrom, sinkTo, place.coast));
    return std::min(land, own + rise);
}
