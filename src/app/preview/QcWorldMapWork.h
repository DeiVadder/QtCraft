#pragma once

#include "worldgen/generators/QcBiomeMap.h"

#include <QImage>
#include <QJsonObject>
#include <QList>

#include <functional>
#include <memory>

class QcContentRegistry;
class QcTerrainGenerator;

// What the map preview of a new world is computed from (decision E-921): the menu's content, the
// world generation options as the form holds them, the seed, the square shown, whether villages
// and structures are wanted and whether the map itself is (not when only the sites of the map
// already shown are asked for).
struct QcWorldMapRequest
{
    enum class Passes : quint8 { All, SitesOnly };

    std::shared_ptr<const QcContentRegistry> content;
    QJsonObject values;
    qint64 seed = 0;
    QcBiomeMap::Area area;
    bool sites = false;
    Passes passes = Passes::All;

    bool operator==(const QcWorldMapRequest &) const = default;
    // Whether `other` describes the same world (only the view differs).
    [[nodiscard]] bool sameWorld(const QcWorldMapRequest &other) const
    {
        return content == other.content && values == other.values && seed == other.seed;
    }
};

// One step of a preview: the coarse map, the refined one, then (when wanted) the sites, then the end.
struct QcWorldMapResult
{
    // Done ends a build that was not cancelled (also one with nothing to show).
    enum class Pass : quint8 { Coarse, Fine, Sites, Done };

    quint64 build = 0;
    Pass pass = Pass::Coarse;
    QcBiomeMap::Area area;
    // The drawn map, one pixel per cell (Coarse and Fine).
    QImage image;
    // What covers each cell, row by row (Coarse and Fine): its biome, or seaCell / iceCell; the GUI
    // thread names the cell under the pointer from it.
    static constexpr qint16 seaCell = -1;
    static constexpr qint16 iceCell = -2;
    QList<qint16> cells;
    // Cells per biome (Coarse and Fine), cells of open sea and lakes and of frozen ones.
    QList<int> biomeCells;
    int seaCells = 0;
    int iceCells = 0;
    // The colour of each biome on the map, 0xRRGGBB.
    QList<quint32> biomeColours;
    QList<QcBiomeMap::Site> sites;
    // Every kind of site the generator can place, in the order of QcBiomeMap::siteKinds (Sites).
    QStringList siteKinds;
    // How long the step took, in ms (the generator's set-up included in the first step).
    qint64 milliseconds = 0;
};

// The worker side of the map preview: builds the generator exactly as a world created from the
// form gets it (QcWorldOptions::valuesToStore, QcWorldOptions::apply, QcTerrainGenerator with the
// seed) and samples it in passes. Lives on the preview's single worker thread; keeps the last
// generator, so moving or zooming the view of the same world does not set it up again.
class QcWorldMapWorker
{
public:
    // Cells along a side of the coarse and the fine pass.
    static constexpr int coarseCells = 64;
    static constexpr int fineCells = 256;
    // Villages and structures are looked for at most this far from the view's centre (half the
    // side of a square): planning their regions over 4096 × 4096 blocks takes about 3.7 s in a
    // debug build, over 8192 × 8192 about 12 s and overflows the plan caches (decision E-921).
    static constexpr int sitesRadius = 1024;

    using Deliver = std::function<void(QcWorldMapResult)>;

    // Runs the passes of `request` (`build`), handing each result to `deliver`; stops as soon as
    // `cancelled` says so.
    void run(const QcWorldMapRequest &request, quint64 build, const QcBiomeMap::Cancelled &cancelled, const Deliver &deliver);
    // Lets go of the last generator and the content it was made from (the preview was hidden or
    // released): the menu's content, its copy with the options set and the generator's plan caches.
    void forget();

    // The generator a world created with `request`'s content, values and seed gets; null when the
    // values are refused.
    [[nodiscard]] static std::shared_ptr<const QcTerrainGenerator> generatorFor(const QcWorldMapRequest &request);
    // `area` with as many blocks per cell as `cells` along a side need (at least 1).
    [[nodiscard]] static QcBiomeMap::Area withCells(QcBiomeMap::Area area, int cells);
    // The part of `area` searched for villages and structures (at most sitesRadius around its centre).
    [[nodiscard]] static QcBiomeMap::Area sitesArea(QcBiomeMap::Area area);

private:
    [[nodiscard]] std::shared_ptr<const QcTerrainGenerator> generator(const QcWorldMapRequest &request);
    // The request the cached generator was made for.
    const QcWorldMapRequest &world() const { return m_world; }
    void setWorld(QcWorldMapRequest world) { m_world = std::move(world); }

    QcWorldMapRequest m_world;
    std::shared_ptr<const QcTerrainGenerator> m_generator; // NOLINT(qc-member-access): the cache of generator(), used only there and in forget()
};
