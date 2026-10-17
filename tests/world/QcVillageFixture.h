#pragma once

#include "worldgen/generators/QcWorldGenerator.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <QList>
#include <QString>

#include <memory>

// Hand-made villages for the tests of villagers and village life (decision E-424): a village of one
// style built from a synthetic plan on flat ground, independent of the world generator and its seed.
// Like the showcase rows (QcShowcaseVillageArea) it holds every building of its style once with all
// its optional parts (houses with their beds and doors, ladders up to beds upstairs, workstations, a
// field, the well with its bell), but along both sides of one straight road, every building facing
// the road: the road runs east with the buildings north of it facing south (Options::turns 0, the
// buildings south of it turned 180°) or south of it facing north (2); or it runs south with the
// buildings east of it facing west (1, turned 90°, those west of it 270°) or west of it facing east
// (3). Each building lands on the same side in every variant, so the four together turn every
// building all four ways. A style over water stands on stilts
// above a shallow lake with its road as a boardwalk (decision E-407). The plan lists beds,
// workstations, fields and the creatures of the pens like a generated one (QcPiecePlacer).
namespace QcVillageFixture {

// What a fixture village holds and where it lies.
struct Options
{
    QString style = QStringLiteral("qtcraft:plains");
    qint64 id = 1;
    // The start of its road (its west or north end); the buildings lie on both sides of it.
    int x = 0;
    int z = 0;
    // Quarter turns (clockwise from above) of the buildings on the first side of the road, the
    // well's side; those on the other side get half a turn more. Even: the road runs east; odd: south.
    int turns = 0;
    // `spread` turns its i-th village i quarter turns more than `turns`.
    bool turnEach = false;
    // Every house (a building with a bed) this many times: more beds without other buildings.
    int houseCopies = 1;
    // Residents move in even while creatures do not spawn (QcVillagePlan::alwaysLivedIn).
    bool alwaysLivedIn = false;
};

// Blocks between two villages laid out by `spread`: beyond each one's reach (QcVillagePlan::reach),
// so that neither lists the other's beds.
inline constexpr int villageGap = 2 * QcVillagePlan::reach + 1;

// The height of the road and of the floors on land: the top block of the flat ground.
inline constexpr int landFloor = QcWorld::seaLevel + 2;
// Over water: the ground under the lake, and the water surface (the block below the sea level, as in
// QcBoardwalkPlanner).
inline constexpr int lakeBottom = QcWorld::seaLevel - 4;
inline constexpr int waterSurface = QcWorld::seaLevel - 1;

// The plan of a fixture village; null when the content has no such style.
[[nodiscard]] std::shared_ptr<QcVillagePlan> plan(const QcContentRegistry &content, const Options &options);

// Villages of the given styles in a grid of `columns` from (x, z) eastwards and southwards, each with
// `villageGap` blocks around it and the ids 1, 2, …; `base` sets everything else.
[[nodiscard]] QList<std::shared_ptr<const QcVillagePlan>> spread(const QcContentRegistry &content, const QStringList &styles,
                                                                 int columns, const Options &base = {});

// The chunks the village covers, and those within `ring` chunks around them.
[[nodiscard]] QList<QcChunkPos> chunksOf(const QcVillagePlan &plan, int ring = 0);

// The beds of the plan whose villager reaches them over a ladder (a bed a storey above the floor).
[[nodiscard]] qsizetype upstairsBeds(const QcVillagePlan &plan);

// Flat ground with the fixture villages built into it (thread-safe, as the server's generation pool
// needs): grass on land, a lake under villages on stilts. The players spawn at `spawn`, by default
// on the road of the first village.
class Terrain : public QcWorldGenerator
{
public:
    Terrain(std::shared_ptr<const QcContentRegistry> content, QList<std::shared_ptr<const QcVillagePlan>> villages);

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override;
    QcBlockPos spawnPoint() const override;
    std::shared_ptr<const QcVillagePlan> villageFor(QcChunkPos pos) const override;
    std::shared_ptr<const QcVillagePlan> nearestVillage(QcBlockPos from, int reach) const override;

    const QList<std::shared_ptr<const QcVillagePlan>> &villages() const { return m_villages; }
    void setSpawn(QcBlockPos spawn) { m_spawn = spawn; }

private:
    // Whether the column lies under a village on stilts (its lake).
    bool inLake(int x, int z) const;
    void buildGround(QcChunk &chunk) const;
    // One column of the ground (local x and z in the chunk): stone, dirt, grass or under water dirt.
    void buildColumn(QcChunk &chunk, int lx, int lz) const;
    void buildVillage(QcChunk &chunk, const QcVillagePlan &plan) const;
    // The posts under a piece of a village on stilts.
    void buildStilts(QcChunk &chunk, const QcStructurePlacement &piece, const QcVillagePlan &plan) const;
    // A post of `post` (air: none) under the floor cell at world `floor`, down to the ground, if it
    // lies in the chunk.
    void buildPost(QcChunk &chunk, QcBlockPos floor, QcBlockId post) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    QList<std::shared_ptr<const QcVillagePlan>> m_villages;
    QcBlockPos m_spawn;
    QcBlockId m_stone = qcAirBlockId;
    QcBlockId m_dirt = qcAirBlockId;
    QcBlockId m_grass = qcAirBlockId;
    QcBlockId m_water = qcAirBlockId;
};

} // namespace QcVillageFixture
