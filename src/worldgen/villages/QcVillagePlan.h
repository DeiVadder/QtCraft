#pragma once

#include "worldgen/structures/QcStructurePlacement.h"
#include "worldgen/villages/QcVillageStyles.h"

#include <QList>
#include <QSet>
#include <QString>

// One village as the world generator builds it (decision E-403): a pure function of the seed, the
// region and the content, computed from the terrain noise alone (never from generated chunks), so
// every chunk writes its own columns of it and a village spanning chunks comes out the same in any
// generation order (QcVillageGenerator::build).
struct QcVillagePlan
{
    // Beds and workstations this far outside the generated area still belong to the village
    // (QcVillage::reach); the showcase keeps its villages further apart than that.
    static constexpr int reach = 16;

    // A bed head or workstation in world coordinates.
    using Spot = QcVillageSpot;

    // One cell of a jetty's walkway (decision E-404) or of a boardwalk (decision E-407): `block` at `at`, the two cells above it
    // cleared, and with a post (`post` not air) filling the column below it down to the ground.
    struct Deck
    {
        QcBlockPos at;
        QcBlockId block = qcAirBlockId;
        QcBlockId post = qcAirBlockId;
    };

    // A slab on a road cell next to a road cell one block higher (decision E-406): `block` at `at`,
    // the cell above the road's top block.
    struct Step
    {
        QcBlockPos at;
        QcBlockId block = qcAirBlockId;
    };

    // A rectangle of columns (inclusive corners).
    struct Area
    {
        int x0 = 0;
        int z0 = 0;
        int x1 = 0;
        int z1 = 0;

        bool contains(int x, int z) const { return x >= x0 && x <= x1 && z >= z0 && z <= z1; }
    };

    // The ground a village over water shapes around itself (decision E-407, QcWetlandShaper): its
    // settings, the areas it spreads from (pieces and boardwalks) and the land access kept dry
    // (beside and beyond the stairs). `settings.bottom` air: none.
    struct Wetland
    {
        QcVillageStyle::Wetland settings;
        QList<Area> wet;
        QList<Area> dry;
    };

    // A straight run of a road's middle line (decision E-408): `length` cells from (x, z) in direction
    // (dx, dz), one of eight (diagonals are staircases). The runs of one road share its `arm` number;
    // a road bends or winds where one run ends and the next begins. For checks and tools: the
    // chunks build `roads`, `branchRoads` and `decks`.
    struct Run
    {
        int x = 0;
        int z = 0;
        int dx = 0;
        int dz = 0;
        int length = 0;
        int arm = 0;
        bool branch = false;
    };

    // A creature of a piece (an animal pen's pair) in world coordinates; the server spawns it,
    // persistent, when its chunk is seen for the first time.
    struct Mob
    {
        QString mob;
        QcVec3 at;
        // Stays in its enclosure (QcStructureDefinition::Mob::confined, decision E-460).
        bool confined = false;
        // Without a mind (QcStructureDefinition::Mob::noAi, decision E-510).
        bool noAi = false;
    };

    // The id of the village planned in region (x, z) (QcVillagePlanner::plan): bit 61 set, bits 62
    // and 63 clear, the region's x in bits 24 to 47 and its z in bits 0 to 23 (each 24 bits, two's
    // complement). Never 0, which stands for no village (QcVillager::village), and never a
    // settlement's id (QcVillage::settlementId, bit 62).
    static constexpr qint64 regionId(int x, int z)
    {
        constexpr quint64 mask = (quint64{1} << 24) - 1;
        return static_cast<qint64>((quint64{1} << 61) | ((static_cast<quint64>(static_cast<quint32>(x)) & mask) << 24)
            | (static_cast<quint64>(static_cast<quint32>(z)) & mask));
    }
    // The region x and z of a regionId().
    static constexpr int regionX(qint64 id)
    {
        return static_cast<int>(static_cast<qint32>(static_cast<quint32>((static_cast<quint64>(id) >> 24) & 0xffffff) << 8) >> 8);
    }
    static constexpr int regionZ(qint64 id)
    {
        return static_cast<int>(static_cast<qint32>(static_cast<quint32>(static_cast<quint64>(id) & 0xffffff) << 8) >> 8);
    }

    // regionId() of its region; the showcase and test fixtures number their villages from 1.
    qint64 id = 0;
    QString style;
    // The meeting point: the bell of the centre piece, else the centre's ground.
    QcBlockPos centre;
    // Columns covered by pieces, roads and jetties (inclusive), over water with the wetland's reach.
    int minX = 0;
    int minZ = 0;
    int maxX = 0;
    int maxZ = 0;
    // The pieces (centre, houses, farms, lamps), each turned towards its road: the first layer
    // replaces the ground's top block, the foundation fills the ground below it down to solid
    // ground and the terrain above it is cleared.
    QList<QcStructurePlacement> pieces;
    // Road cells: the block that becomes the path block, of the roads from the centre and of their
    // branches; the ground's top block, or across the road the height of its middle line where that
    // lies at most one block off (decision E-409).
    QList<QcBlockPos> roads;
    QList<QcBlockPos> branchRoads;
    // Doorsteps (decision E-409): on land, the cell in front of each outer door of a piece at the
    // height of its floor; the ground there is levelled to it and it is a side road cell too.
    QList<QcBlockPos> thresholds;
    // The street plan it was laid out with (QcVillageLayoutParser::kindName) and whether its roads
    // wind with the terrain (decision E-408).
    QString layout;
    bool winding = false;
    QList<Run> runs;
    // The length of all roads' middle lines in blocks: the village's size for the rules (decision
    // E-408).
    int roadLength = 0;
    // The surfaces chosen for this village from the style's (decision E-406).
    QcBlockId pathBlock = qcAirBlockId;
    QcBlockId branchPathBlock = qcAirBlockId;
    QList<Step> steps;
    QcBlockId foundation = qcAirBlockId;
    // Over water (decision E-407): pieces stand on posts of this block every `stiltSpacing` cells
    // (and at their far edges) down to the bottom instead of on a foundation; air on land. The
    // boardwalks and their stairs are decks.
    QcBlockId stiltPost = qcAirBlockId;
    int stiltSpacing = 3;
    QList<Deck> decks;
    // The decks of the bridges that carry roads on land over rivers (decision E-841).
    QList<Deck> bridges;
    Wetland wetland;
    QList<Spot> beds;
    QList<Spot> workstations;
    // The crop cells of its pieces in the world: its fields, the only crops its farmers harvest
    // (decision E-411).
    QSet<QcBlockPos> fields;
    QList<Mob> mobs;
    // Residents move into its free beds even while creatures do not spawn (the rows of the showcase
    // world, decision E-411); other villages get theirs only with the creatures.
    bool alwaysLivedIn = false;
    // Its residents, the ones moving into its beds and the children born there, are confined
    // (QcCreature::confined, decision E-510): they change no block, so the rows of the showcase
    // world keep their ripe fields with the game rule mobGriefing on.
    bool residentsConfined = false;

    bool covers(int x, int z, int margin) const
    {
        return x >= minX - margin && x <= maxX + margin && z >= minZ - margin && z <= maxZ + margin;
    }
    bool overlaps(QcChunkPos chunk, int margin) const
    {
        const int x0 = chunk.x * QcWorld::chunkSize;
        const int z0 = chunk.z * QcWorld::chunkSize;
        return x0 + QcWorld::chunkSize - 1 >= minX - margin && x0 <= maxX + margin && z0 + QcWorld::chunkSize - 1 >= minZ - margin
               && z0 <= maxZ + margin;
    }

    // The lists of a finished plan without the room they grew into: the generator caches hundreds of
    // plans (QcVillageGenerator::cachedPlans).
    void squeeze()
    {
        pieces.squeeze();
        roads.squeeze();
        branchRoads.squeeze();
        thresholds.squeeze();
        runs.squeeze();
        steps.squeeze();
        decks.squeeze();
        bridges.squeeze();
        wetland.wet.squeeze();
        wetland.dry.squeeze();
        beds.squeeze();
        workstations.squeeze();
        mobs.squeeze();
    }
};
