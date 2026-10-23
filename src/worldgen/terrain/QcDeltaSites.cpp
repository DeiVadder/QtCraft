#include "worldgen/terrain/QcDeltaSites.h"

#include "worldgen/terrain/QcTerrainMath.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <unordered_map>

using QcTerrainMath::smoothstep;

namespace {

// The continent's slope is read over this many blocks.
constexpr int coastStep = 4;
// The steps down the river to the mouth: at most mouthSteps of at most longestStep blocks, until
// the coast is less than `settled` blocks away.
constexpr int mouthSteps = 10;
constexpr double longestStep = 64.0;
constexpr double settled = 2.0;
// Searched from the apex, the mouth lies at most this many blocks from where it was found.
constexpr double agree = 8.0;
// The sites are searched from the middle of cells of this many blocks; the last ones searched (at
// most maxRemembered) are kept per thread.
constexpr double siteCell = 8.0;
// A cell reaches the river from this many blocks away (the widest fan's side with its margin and
// a cell more).
constexpr double cellReach = 120.0;
constexpr std::size_t maxRemembered = 1 << 14;

// Deltas are rare: only where a chance noise on a large scale lies above the quantile of the
// share of mouths that get one; the gate opens over this band of noise values round the quantile.
constexpr double chanceScale = 1100.0;
constexpr double chanceBand = 0.05;
// Quantiles of the chance noise (two octaves; measured over 450 000 points of five seeds): the
// share of the map above each value.
struct Quantile
{
    double share = 0.0;
    double value = 0.0;
};
constexpr std::array<Quantile, 13> chanceQuantiles{{{0.0, 0.79},
                                                    {0.02, 0.453},
                                                    {0.05, 0.371},
                                                    {0.1, 0.295},
                                                    {0.15, 0.24},
                                                    {0.2, 0.196},
                                                    {0.3, 0.123},
                                                    {0.5, 0.0},
                                                    {0.7, -0.127},
                                                    {0.8, -0.2},
                                                    {0.9, -0.298},
                                                    {0.95, -0.376},
                                                    {1.0, -0.79}}};

// The noise value above which the given share of the map lies (linear between the quantiles).
double chanceQuantile(double share)
{
    const auto above = std::ranges::find_if(chanceQuantiles, [share](Quantile q) { return q.share >= share; });
    if (above == chanceQuantiles.begin()) {
        return above->value;
    }
    if (above == chanceQuantiles.end()) {
        return chanceQuantiles.back().value;
    }
    const Quantile &below = *(above - 1);
    return below.value + (share - below.share) / (above->share - below.share) * (above->value - below.value);
}
// The traits of a delta: the chance noise read at its mouth on this scale, one reading apart per
// trait.
constexpr double traitScale = 400.0;
constexpr double traitApart = 37.1;
// Only in flat land (not through mountain ranges, gorges or cliffs; rivers run in gorges from a
// ruggedness of 0.3 on, E-840).
constexpr double flatTo = 0.3;
constexpr double flatFrom = 0.4;
// The river runs into the sea, not along the coast: the share of the continent's slope along the
// river at the mouth (1 straight into the sea, 0 along the coast).
constexpr double straightFrom = 0.35;
constexpr double straightTo = 0.55;
// ... and about straight from the apex to the mouth: the cosine of the angle between its
// directions there, and how many blocks the river halfway up may lie aside of the straight line.
constexpr double bendFrom = 0.65;
constexpr double bendTo = 0.8;
constexpr double asideFrom = 15.0;
constexpr double asideTo = 25.0;
// The mouth lies on the coast: a continent value this far from the coast's there means the river
// never reaches it (the steps down the river did not settle).
constexpr double missFrom = 0.02;
constexpr double missTo = 0.04;
// Beyond the mouth lies the open sea, not a lake or low land: this many blocks further down the
// river the continent lies below openSeaFrom (there the ground lies some 5 blocks under the sea).
constexpr double openSeaBeyond = 50.0;
constexpr double openSeaFrom = -0.2;
constexpr double openSeaTo = -0.17;
// Only wide rivers get a delta: half the width of their water at the mouth.
constexpr double wideFrom = 4.0;
constexpr double wideTo = 4.6;

// Which search a thread remembers: the sites (each instance its own) and the cell.
struct SiteKey
{
    quint64 sites = 0;
    qint32 cellX = 0;
    qint32 cellZ = 0;

    bool operator==(const SiteKey &) const = default;
};

struct SiteKeyHash
{
    std::size_t operator()(SiteKey key) const
    {
        const quint64 cell = (quint64(quint32(key.cellX)) << 32) | quint32(key.cellZ);
        return std::size_t(qcMixSeed(key.sites ^ cell, 3));
    }
};

} // namespace

quint64 QcDeltaSites::nextInstance()
{
    static std::atomic<quint64> instances{0};
    return ++instances;
}

QcDeltaSites::QcDeltaSites(quint64 seed, const QcRiverField &rivers, double share)
    : m_instance(nextInstance())
    , m_rivers(rivers)
    , m_chanceNoise(qcMixSeed(seed, 54))
    , m_chanceAbove(chanceQuantile(std::clamp(share, 0.0, 1.0)))
    , m_everyMouth(share >= 1.0)
{
}

double QcDeltaSites::chanceAt(double mouthX, double mouthZ) const
{
    if (m_everyMouth) {
        return 1.0;
    }
    const double chance = m_chanceNoise.fractal2(mouthX / chanceScale, mouthZ / chanceScale, 2);
    return smoothstep(m_chanceAbove - 0.5 * chanceBand, m_chanceAbove + 0.5 * chanceBand, chance);
}

std::shared_ptr<const QcDeltaSites::Site> QcDeltaSites::siteNear(double x, double z, const Land &land) const
{
    // A cell whose own search finds no delta takes the one of a neighbouring cell (the searches
    // from far out at sea or far from the river fail now and then; their cells would leave square
    // gaps in a fan). Each is a pure function of the sites and the cells, so the order of
    // generation does not matter.
    const SiteKey key{m_instance, qint32(std::floor(x / siteCell)), qint32(std::floor(z / siteCell))};
    thread_local std::unordered_map<SiteKey, std::shared_ptr<const Site>, SiteKeyHash> remembered;
    if (const auto found = remembered.find(key); found != remembered.end()) {
        return found->second;
    }
    if (remembered.size() >= maxRemembered) {
        remembered.clear();
    }
    std::shared_ptr<const Site> site = cellSite(key.cellX, key.cellZ, land);
    site = site ? site : ringSite(key.cellX, key.cellZ, land);
    remembered.emplace(key, site);
    return site;
}

std::shared_ptr<const QcDeltaSites::Site> QcDeltaSites::ringSite(qint32 cellX, qint32 cellZ, const Land &land) const
{
    // Ring by ring, in a fixed order: the nearest cell with a delta (the first ring, then the second).
    static constexpr std::array<std::pair<qint32, qint32>, 24> rings{{{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}, {-2, -2}, {-1, -2}, {0, -2}, {1, -2}, {2, -2}, {-2, -1}, {2, -1}, {-2, 0}, {2, 0}, {-2, 1}, {2, 1}, {-2, 2}, {-1, 2}, {0, 2}, {1, 2}, {2, 2}}};
    for (const auto [dx, dz] : rings) {
        if (std::shared_ptr<const Site> site = cellSite(cellX + dx, cellZ + dz, land)) {
            return site;
        }
    }
    return nullptr;
}

std::shared_ptr<const QcDeltaSites::Site> QcDeltaSites::cellSite(qint32 cellX, qint32 cellZ, const Land &land) const
{
    // Every point of a cell searches from its middle.
    const SiteKey key{m_instance, cellX, cellZ};
    thread_local std::unordered_map<SiteKey, std::shared_ptr<const Site>, SiteKeyHash> searched;
    if (const auto found = searched.find(key); found != searched.end()) {
        return found->second;
    }
    if (searched.size() >= maxRemembered) {
        searched.clear();
    }
    const std::optional<Site> found = search((cellX + 0.5) * siteCell, (cellZ + 0.5) * siteCell, land);
    const std::shared_ptr<const Site> site = found ? std::make_shared<const Site>(*found) : nullptr;
    searched.emplace(key, site);
    return site;
}

std::optional<QcDeltaSites::Reach> QcDeltaSites::reachFrom(double x, double z, const Land &land, double within) const
{
    // Onto the river's middle line from where the noise's slope points, and the river's direction
    // there.
    const int fromX = int(std::lround(x));
    const int fromZ = int(std::lround(z));
    const QcRiverField::Course course = m_rivers.courseAt(fromX, fromZ, m_rivers.valueAt(fromX, fromZ));
    if (std::abs(course.offset) >= within) {
        return std::nullopt;
    }
    Reach reach;
    reach.x = fromX - course.offset * course.acrossX;
    reach.z = fromZ - course.offset * course.acrossZ;
    const int atX = int(std::lround(reach.x));
    const int atZ = int(std::lround(reach.z));
    const double continent = land.continentAt(atX, atZ);
    const double coastX = (land.continentAt(atX + coastStep, atZ) - continent) / coastStep;
    const double coastZ = (land.continentAt(atX, atZ + coastStep) - continent) / coastStep;
    // The continent's rise along the river (the direction across it turned by a right angle).
    const double rise = coastZ * course.acrossX - coastX * course.acrossZ;
    reach.slope = std::hypot(coastX, coastZ);
    if (rise == 0.0) {
        return std::nullopt;
    }
    reach.inland = (continent - coastContinent) / std::abs(rise);
    reach.straight = std::abs(rise) / reach.slope;
    const double sign = rise > 0.0 ? 1.0 : -1.0;
    reach.downX = sign * course.acrossZ;
    reach.downZ = -sign * course.acrossX;
    return reach;
}

std::optional<QcDeltaSites::Reach> QcDeltaSites::mouthFrom(double x, double z, const Land &land) const
{
    // Down the river to where the continent's slope puts the coast, until the steps settle on the
    // mouth (every column of a delta then finds the same one); none where they do not.
    // Steps are at most longestStep blocks: where the continent is nearly flat (far out at sea) its
    // slope would throw the first steps far off.
    std::optional<Reach> coast = reachFrom(x, z, land, cellReach);
    for (int step = 0; coast && std::abs(coast->inland) > settled; ++step) {
        const double length = std::clamp(coast->inland, -longestStep, longestStep);
        coast = step < mouthSteps ? reachFrom(coast->x + length * coast->downX, coast->z + length * coast->downZ, land, QcRiverField::reach)
                                  : std::nullopt;
    }
    return coast && std::abs(coast->inland) <= settled ? coast : std::nullopt;
}

std::optional<QcDeltaSites::Site> QcDeltaSites::search(double x, double z, const Land &land) const
{
    const std::optional<Reach> coast = mouthFrom(x, z, land);
    if (!coast) {
        return std::nullopt;
    }
    Site site;
    site.mouthX = coast->x + coast->inland * coast->downX;
    site.mouthZ = coast->z + coast->inland * coast->downZ;
    site.slope = coast->slope;

    // Twice half a fan's length up the river from the mouth: the apex, on the river.
    const std::optional<Reach> half = reachFrom(site.mouthX - 0.5 * fanLength * coast->downX, site.mouthZ - 0.5 * fanLength * coast->downZ, land, QcRiverField::reach);
    const std::optional<Reach> apex = half ? reachFrom(half->x - 0.5 * fanLength * half->downX, half->z - 0.5 * fanLength * half->downZ, land, QcRiverField::reach)
                                           : std::nullopt;
    if (!apex) {
        return std::nullopt;
    }
    site.apexX = apex->x;
    site.apexZ = apex->z;
    site.length = std::hypot(site.mouthX - site.apexX, site.mouthZ - site.apexZ);
    if (site.length < 0.5 * fanLength) {
        return std::nullopt;
    }
    site.downX = (site.mouthX - site.apexX) / site.length;
    site.downZ = (site.mouthZ - site.apexZ) / site.length;
    // From the apex down the river the same mouth: the whole fan agrees on it (not where the river
    // turns along the coast, or meets it twice).
    const std::optional<Reach> again = mouthFrom(site.apexX, site.apexZ, land);
    if (!again || std::hypot(again->x + again->inland * again->downX - site.mouthX, again->z + again->inland * again->downZ - site.mouthZ) > agree) {
        return std::nullopt;
    }

    // The river runs about straight from the apex to the mouth: the same direction at both ends,
    // and the point halfway up close to the straight line between them.
    const double aside = std::abs((half->x - site.apexX) * site.downZ - (half->z - site.apexZ) * site.downX);
    const Verdict verdict = verdictAt(*coast, site.mouthX, site.mouthZ, land);
    site.allowed = verdict.allowed * smoothstep(bendFrom, bendTo, apex->downX * coast->downX + apex->downZ * coast->downZ)
                   * (1.0 - smoothstep(asideFrom, asideTo, aside));
    site.riverHalfWidth = verdict.riverHalfWidth;
    if (site.allowed <= 0.0) {
        return std::nullopt;
    }
    for (int i = 0; i < traitCount; ++i) {
        const double reading = m_chanceNoise.fractal2(site.mouthX / traitScale + traitApart * i, site.mouthZ / traitScale - traitApart * i, 1);
        site.traits[std::size_t(i)] = smoothstep(-0.45, 0.45, reading);
    }
    return site;
}

QcDeltaSites::Verdict QcDeltaSites::verdictAt(const Reach &coast, double mouthX, double mouthZ, const Land &land) const
{
    const int atX = int(std::lround(mouthX));
    const int atZ = int(std::lround(mouthZ));
    const QcTerrainClimate::Point climate = land.climateAt(atX, atZ);
    double allowed = QcRiverField::strengthAt(climate) * (1.0 - smoothstep(flatTo, flatFrom, climate.ruggedness))
                     * chanceAt(mouthX, mouthZ)
                     * smoothstep(straightFrom, straightTo, coast.straight);
    if (allowed <= 0.0) {
        return {};
    }
    allowed *= 1.0 - smoothstep(missFrom, missTo, std::abs(land.continentAt(atX, atZ) - coastContinent));
    const int seaX = int(std::lround(mouthX + openSeaBeyond * coast.downX));
    const int seaZ = int(std::lround(mouthZ + openSeaBeyond * coast.downZ));
    allowed *= allowed > 0.0 ? 1.0 - smoothstep(openSeaFrom, openSeaTo, land.continentAt(seaX, seaZ)) : 0.0;
    if (allowed <= 0.0) {
        return {};
    }
    const QcRiverField::Sample river = m_rivers.at(atX, atZ, climate);
    return {allowed * smoothstep(wideFrom, wideTo, river.halfWidth), river.halfWidth};
}
