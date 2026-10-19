#pragma once

#include "worldgen/generators/QcChunkFurnishing.h"
#include "worldgen/generators/QcGuardSite.h"
#include "worldgen/villages/QcVillagePlan.h"
#include "worldgen/villages/QcVillagePlanCache.h"
#include "worldgen/villages/QcVillagePlanner.h"
#include "worldgen/villages/QcVillageStyles.h"
#include "worldgen/villages/QcWetlandShaper.h"

#include <memory>

class QcTerrainGenerator;

// The villages of a terrain world (decision E-403): plans each region's village once (cached,
// shared by the generation threads and the server thread, decision E-416) and writes the part of
// it that lies in a chunk. A chunk lies in exactly one region and a village never leaves its
// region, so a chunk needs only its own region's plan. Thread-safe.
class QcVillageGenerator
{
public:
    // Regions the cache keeps (least recently used go first). A village's plan holds about 33 KB
    // (the largest about 55 KB: the structures of its varied buildings, its road cells) and about
    // one region in six has one, so a full cache holds about 3 MB, at most about 28 MB
    // (test_village_measure). It keeps the 13 x 13 regions /locate village searches and those
    // around the players generating.
    static constexpr qsizetype cachedPlans = 512;

    // What the styles leave out is in styles().warnings().
    QcVillageGenerator(const QcTerrainGenerator &terrain, std::shared_ptr<const QcContentRegistry> content, quint64 seed);

    // The village of the region that holds the chunk; null when it has none.
    [[nodiscard]] std::shared_ptr<const QcVillagePlan> planFor(QcChunkPos chunk) const;
    [[nodiscard]] std::shared_ptr<const QcVillagePlan> regionPlan(int regionX, int regionZ) const;
    // The village whose centre lies nearest to `from` within `reach` blocks (whole regions), if any.
    [[nodiscard]] std::shared_ptr<const QcVillagePlan> nearest(QcBlockPos from, int reach) const;
    // The village piece of structure `name` (the temple at a desert village's centre) whose middle
    // lies nearest to `from` within `reach` blocks (whole regions), if any.
    [[nodiscard]] std::optional<QcStructurePlacement> nearestPiece(const QString &name, QcBlockPos from, int reach) const;
    // Writes the roads, jetties and pieces of `plan` that lie in the chunk: roads into the ground's top
    // block, pieces after clearing the terrain above their footprint and filling the ground below it;
    // over water the wetland is shaped first (QcWetlandShaper).
    void build(QcChunk &chunk, const QcVillagePlan &plan) const;
    // The loot of the chests of `plan`'s pieces in the chunk (decision E-590: the temple at the
    // centre of a desert village).
    [[nodiscard]] static QcChunkFurnishing furnishingIn(QcChunkPos pos, const QcVillagePlan &plan);
    // The guardian zones (decision E-591) of the pieces of the villages in the regions within
    // `reach` of `from`.
    QList<QcGuardSite> sitesNear(QcBlockPos from, int reach) const;
    // The same from the regions planned so far, never planning (the server's tick thread);
    // std::nullopt when a region within `reach` is not planned yet.
    [[nodiscard]] std::optional<QList<QcGuardSite>> plannedSitesNear(QcBlockPos from, int reach) const;
    // The guardian zones of the pieces of one village.
    static QList<QcGuardSite> sitesOf(const QcVillagePlan &plan);
    const QcVillageStyles &styles() const { return m_styles; }
    const QcWetlandShaper &wetland() const { return m_wetland; }
    const QcVillagePlanCache &cache() const { return m_cache; }
    int spacing() const { return m_planner.spacing(); }

private:
    // The guardian zones of the regions within `reach` of `from`; with `mayPlan` false only from
    // planned regions (std::nullopt when one is not planned yet).
    [[nodiscard]] std::optional<QList<QcGuardSite>> collectSites(QcBlockPos from, int reach, bool mayPlan) const;
    void buildPiece(QcChunk &chunk, const QcStructurePlacement &piece, const QcVillagePlan &plan) const;
    // Over water (decision E-407): whether a world column of a piece gets a post, every `spacing`
    // cells from its north west corner and at its far edges.
    static bool isStiltColumn(const QcStructurePlacement &piece, int x, int z, int spacing);
    // A post under a piece's floor block at `floor` (local in the chunk) down to the ground.
    void buildPost(QcChunk &chunk, QcBlockPos floor, QcBlockId post) const;
    // Clears the terrain above a piece's floor in one column of the chunk (x and z of `local`) and
    // fills the ground below it with the foundation (air: none) down to solid ground.
    void prepareColumn(QcChunk &chunk, QcBlockPos local, const QcStructurePlacement &piece, QcBlockId foundation) const;
    // Levels the ground of the doorsteps that lie in the chunk to their height: the terrain above
    // cleared, the foundation filled in below (decision E-409).
    void buildThresholds(QcChunk &chunk, const QList<QcBlockPos> &thresholds, QcBlockId foundation) const;
    // Lays the road cells that lie in the chunk at their height: into the ground's top block, onto it
    // where the cell lies one higher, the terrain above cleared.
    void buildRoads(QcChunk &chunk, const QList<QcBlockPos> &cells, QcBlockId path) const;
    // Lays the steps of the roads (slabs where a road rises by one block) that lie in the chunk.
    void buildSteps(QcChunk &chunk, const QList<QcVillagePlan::Step> &steps) const;
    // Lays the jetty cells that lie in the chunk, with the posts under them down to the ground.
    void buildDecks(QcChunk &chunk, const QList<QcVillagePlan::Deck> &decks) const;
    bool isGround(QcBlockId block) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    QcVillageStyles m_styles;
    QcVillagePlanner m_planner;
    QcWetlandShaper m_wetland;
    QcVillagePlanCache m_cache{cachedPlans};
};
