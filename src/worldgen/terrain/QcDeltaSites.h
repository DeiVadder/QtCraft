#pragma once

#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcRiverField.h"
#include "worldgen/terrain/QcTerrainClimate.h"

#include <QtGlobal>

#include <array>
#include <memory>
#include <optional>

// Where the river deltas lie (decision E-850): from a point on a river, down the river to where it
// meets the coast (its mouth) and up the river to the apex of the fan, and whether that mouth gets
// a delta at all (a wide river through flat land straight into the open sea, and the chance). Every
// column of a delta reaches the same mouth, so a delta is decided and shaped alike for all of them.
// Deterministic per seed and thread-safe; the sites are remembered per thread by cells of a few
// blocks along the rivers.
class QcDeltaSites
{
public:
    // The terrain's maps a delta reads (QcTerrainGenerator).
    using Land = QcRiverField::Land;

    // A delta: the mouth of its river (on the coast) and the apex of its fan (on the river, about
    // a fan's length up from the mouth), the unit direction from the apex to the mouth and their
    // distance, how fast the continent rises towards the land at the mouth (per block), how much
    // of a delta the mouth allows (0..1), half the width of the river there and the delta's own
    // random numbers (0..1, the shape of its network of arms; smooth in the mouth, so every column
    // of the delta reads the same ones).
    static constexpr int traitCount = 64;
    struct Site
    {
        double mouthX = 0.0;
        double mouthZ = 0.0;
        double apexX = 0.0;
        double apexZ = 0.0;
        double downX = 0.0;
        double downZ = 0.0;
        double length = 0.0;
        double slope = 0.0;
        double allowed = 0.0;
        double riverHalfWidth = 0.0;
        std::array<double, traitCount> traits{};
    };

    // The continent value of the coast where a delta's river meets the sea.
    static constexpr double coastContinent = -0.14;
    // A fan reaches this many blocks up the river from the mouth.
    static constexpr double fanLength = 110.0;

    // `rivers` must outlive the sites (the terrain generator owns both); `share`: the share of the
    // wide river mouths that get a delta (0..1; 1 every one).
    QcDeltaSites(quint64 seed, const QcRiverField &rivers, double share);

    // The delta of the river near (x, z) (found from the middle of its cell of 8 blocks), null for
    // none; shared with the thread's memory of the sites (a site is too large to copy per column).
    [[nodiscard]] std::shared_ptr<const Site> siteNear(double x, double z, const Land &land) const;

private:
    // The river's middle line reached from a point near it: where, the unit direction down the
    // river (towards the falling continent), how many blocks up the river from the coast the
    // continent's slope puts it, the share of that slope along the river and the slope itself.
    struct Reach
    {
        double x = 0.0;
        double z = 0.0;
        double downX = 0.0;
        double downZ = 0.0;
        double inland = 0.0;
        double straight = 0.0;
        double slope = 0.0;
    };
    // `within`: how many blocks from the river the point may lie.
    [[nodiscard]] std::optional<Reach> reachFrom(double x, double z, const Land &land, double within) const;
    // The river's middle line on the coast, reached from a point near the river by steps down it;
    // std::nullopt where the steps do not settle.
    [[nodiscard]] std::optional<Reach> mouthFrom(double x, double z, const Land &land) const;
    // The delta found from the middle of a cell, remembered per thread.
    // The delta of the nearest cell around (cellX, cellZ) that has one, within a few rings.
    [[nodiscard]] std::shared_ptr<const Site> ringSite(qint32 cellX, qint32 cellZ, const Land &land) const;
    [[nodiscard]] std::shared_ptr<const Site> cellSite(qint32 cellX, qint32 cellZ, const Land &land) const;
    // The search from the middle of a cell.
    [[nodiscard]] std::optional<Site> search(double x, double z, const Land &land) const;
    // How much of a delta the mouth (mouthX, mouthZ), reached as `coast`, allows (0..1): flat land,
    // a climate where rivers run, the chance, a river into the sea, the coast where the slope
    // said, the open sea beyond it and a wide river; and half the width of the river there.
    struct Verdict
    {
        double allowed = 0.0;
        double riverHalfWidth = 0.0;
    };
    [[nodiscard]] Verdict verdictAt(const Reach &coast, double mouthX, double mouthZ, const Land &land) const;

    [[nodiscard]] static quint64 nextInstance();
    // How much of a delta the chance allows at the mouth (0..1).
    [[nodiscard]] double chanceAt(double mouthX, double mouthZ) const;

    quint64 m_instance = 0;
    const QcRiverField &m_rivers;
    QcNoise m_chanceNoise;
    // The chance noise allows a delta above this value (the share's quantile of the noise); every
    // mouth for a share of 1.
    double m_chanceAbove = 0.0;
    bool m_everyMouth = false;
};
