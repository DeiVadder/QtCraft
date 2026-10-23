#pragma once

#include "worldgen/terrain/QcDeltaSites.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcRiverField.h"

#include <QtGlobal>

#include <optional>

// The deltas of the rivers (decision E-850): where a wide river runs through flat land into the
// sea, it spreads into a fan of arms between low islands of sand and mud, with ponds on them and
// shallow banks reaching out under the sea. The fan opens from its apex on the river towards the
// mouth on the coast (QcDeltaSites); its arms are a network that branches two or three levels deep
// from the river (QcDeltaArms), and the fan lies in a low basin that rises gently into the land
// around it. Deterministic per seed and thread-safe.
class QcRiverDelta
{
public:
    using Land = QcDeltaSites::Land;

    // `rivers` must outlive the delta (the terrain generator owns both); `share`: the share of the
    // wide river mouths that get a delta (0..1).
    QcRiverDelta(quint64 seed, const QcRiverField &rivers, int seaLevel, double share);

    // Whether a column with this continent value (the terrain's, before the coast's smoothstep) can
    // lie in a delta at all: a band along the coasts. Free; every other test costs noise.
    [[nodiscard]] static bool mayReach(double continent);

    // A column that may lie in the fan of a delta, before its delta is known (that costs readings
    // of the continent, the climate and the river, remembered per cell): its continent value and
    // how much of a delta the band along the coasts allows (0..1).
    struct Fan
    {
        int x = 0;
        int z = 0;
        double continent = 0.0;
        double allowed = 0.0;
    };
    // The fan at a column with the continent value `continent` and the river noise `riverValue`
    // (QcRiverField::Sample::value, `far` where the river field was not asked); std::nullopt where
    // no delta can lie. Free: reads no noise.
    [[nodiscard]] std::optional<Fan> fanAt(int x, int z, double continent, double riverValue) const;

    // The delta at a column: how much of it there is (0..1, blended over the fan's edges), how much
    // of that is the delta proper (its land and arms, not the banks sunk under the sea further out;
    // the delta's biome where it outweighs the rest) and the height of the ground (unrounded, the
    // first air above it) blended from the column's own: the arms down to a few blocks under the
    // sea level, the islands just above it, under the sea further out.
    struct Shape
    {
        double weight = 0.0;
        double core = 0.0;
        double height = 0.0;
    };
    // The column's own ground before the delta: its height and whether a river put water there
    // (the delta never fills a river's channel, so the river always reaches its arms).
    struct Ground
    {
        double height = 0.0;
        bool river = false;
    };
    // The delta at the column of `fan`, by the delta of the river near its cell.
    [[nodiscard]] Shape shape(const Fan &fan, const Land &land, Ground own) const;

private:
    // Where a column lies in a fan: along it by the river (0 at the apex, 1 at the mouth, more out
    // at sea), along it by its own distance to the coast (the same scale; `inland` the same in
    // blocks, negative out at sea), across it in blocks from
    // the line from the apex to the mouth (bent a little by noise), the fan's half width in blocks
    // there and how many blocks the column lies beyond the fan's side (0 inside).
    struct Place
    {
        double along = 0.0;
        double coast = 0.0;
        double inland = 0.0;
        double lateral = 0.0;
        double halfWidth = 0.0;
        double outside = 0.0;
    };
    // Where the column of `fan` lies in the fan of `site`; std::nullopt outside it.
    [[nodiscard]] std::optional<Place> placeIn(const Fan &fan, const QcDeltaSites::Site &site) const;
    // The land of the delta at a column of `fan` at `place` over the column's own ground `own`:
    // low islands with ponds inside the fan, rising gently beyond its sides, never above the own
    // ground on land and only a little above it under the sea (shoals and bars along the coast).
    [[nodiscard]] double landOf(const Fan &fan, const Place &place, double own) const;

    QcDeltaSites m_sites;
    int m_seaLevel = 0;
    QcNoise m_wiggleNoise;
    QcNoise m_islandNoise;
    QcNoise m_pondNoise;
};
