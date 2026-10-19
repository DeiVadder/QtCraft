#include "common/gameplay/QcCrafting.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcConnection.h"
#include "common/protocol/QcInProcessTransport.h"
#include "common/world/QcDayCycle.h"
#include "common/world/QcLightVolume.h"
#include "QcRawClient.h"
#include "QcTestMachines.h"
#include "QcTestUtil.h"
#include "QcTestWorld.h"
#include "server/core/QcServer.h"
#include "server/mobs/QcMobManager.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// The test terrain, but every column reports the swamp biome.
class SwampGenerator : public QcTestTerrainGenerator
{
public:
    using QcTestTerrainGenerator::QcTestTerrainGenerator;
    QString biomeAt(int, int) const override { return u"qtcraft:swamp"_s; }
};

// The real terrain with the spawn point put where a test wants it.
class SpawnAtGenerator : public QcTerrainGenerator
{
public:
    using QcTerrainGenerator::QcTerrainGenerator;
    QcBlockPos spawnPoint() const override { return spawn; }
    QcBlockPos spawn;
};

constexpr double noon = QcDayCycle::dayLength * 0.25;
constexpr double midnight = QcDayCycle::dayLength * 0.75;

} // namespace

// World details on the server: liquids with buckets, lava, falling blocks, vegetation rules, fireflies
// and the mine lamp, through a raw protocol client.
class TestWorldDetails : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    std::unique_ptr<QcServer> m_server;
    std::unique_ptr<QcRawClient> m_client;
    QcBlockPos m_feet;
    quint32 m_sequence = 1;

    QcBlockId id(const QString &state) const
    {
        const std::optional<QcBlockId> found = m_content->stateId(state.contains(u':') ? state : u"qtcraft:"_s + state);
        if (!found) {
            QcTestUtil::failCase(QStringLiteral("unknown block state %1").arg(state));
            return qcAirBlockId;
        }
        return *found;
    }
    QcItemId item(const char *name) const { return *m_content->itemId(u"qtcraft:"_s + QString::fromLatin1(name)); }
    QcBlockId at(QcBlockPos pos) const { return m_server->world().blockAt(pos).value_or(qcAirBlockId); }
    QString nameAt(QcBlockPos pos) const { return m_content->stateName(at(pos)); }
    bool set(QcBlockPos pos, const QString &state) { return m_server->world().setBlock(pos, id(state)); }

    int count(QcItemId wanted) const
    {
        int total = 0;
        for (const QcItemStack &stack : std::as_const(m_client->stacks)) {
            total += stack.item == wanted ? stack.count : 0;
        }
        return total;
    }
    int dropped(QcItemId wanted) const
    {
        int total = 0;
        for (const QcItemEntity &entity : m_server->items().items()) {
            total += entity.stack.item == wanted ? entity.stack.count : 0;
        }
        return total;
    }

    void ticks(int count) { QcTestUtil::ticks(*m_server, count); }

    void join(QcServerConfig config = {}, bool stage = true)
    {
        config.viewDistance = 1;
        config.operators = {u"Tester"_s};
        // These tests build away from the player (stage 31.3: chunks would leave the world).
        config.keepChunksLoaded = true;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());

        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        m_client = std::make_unique<QcRawClient>(std::move(clientEnd));
        m_client->hello(u"Tester"_s);
        QcTestUtil::pump();
        ticks(2);

        const QcVec3 p = m_client->motion.position;
        m_feet = {int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
        for (int cx = -1; cx <= 1; ++cx) {
            for (int cz = -1; cz <= 1; ++cz) {
                m_server->world().loadChunk({m_feet.chunk().x + cx, m_feet.chunk().z + cz});
            }
        }

        // A flat grass stage on stone with room above.
        for (int dx = -6; dx <= 6 && stage; ++dx) {
            for (int dz = -6; dz <= 6; ++dz) {
                for (int dy = -3; dy <= -2; ++dy) {
                    set(m_feet.offset(dx, dy, dz), u"stone"_s);
                }
                set(m_feet.offset(dx, -1, dz), u"grass_block"_s);
                for (int dy = 0; dy <= 8; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        ticks(1);
    }

    void send(const QcMessage &message)
    {
        m_client->connection.send(message);
        QcTestUtil::pump();
        ticks(1);
    }
    void chat(const QString &text) { send(QcMsg::ChatInput{text}); }
    void face(float yaw, float pitch = 0.0f)
    {
        QcPlayerInput input;
        input.sequence = ++m_sequence;
        input.yaw = yaw;
        input.pitch = pitch;
        send(QcMsg::PlayerInput{input});
    }
    // Gives `count` of an item and selects the slot holding it.
    void hold(const char *name, int count = 1)
    {
        chat(u"/give %1 %2"_s.arg(QString::fromLatin1(name)).arg(count));
        for (int slot = 0; slot < m_client->stacks.size(); ++slot) {
            if (m_client->stacks[slot].item == item(name)) {
                send(QcMsg::SelectHotbarSlot{slot});
                return;
            }
        }
        QFAIL("item not given");
    }
    void use(QcBlockPos target, QcBlockFace face) { send(QcMsg::UseItem{target, face, {0.5, 0.5, 0.5}}); }
    QString cmdPos(QcBlockPos pos) const { return u"%1 %2 %3"_s.arg(pos.x).arg(pos.y).arg(pos.z); }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void bucketsTakeAndPourLiquids()
    {
        join();
        const QcBlockPos pool = m_feet.offset(2, -1, 0);
        set(pool, u"water"_s);
        hold("bucket", 2);
        use(pool, QcBlockFace::Up);
        QCOMPARE(nameAt(pool), u"qtcraft:air"_s);
        QCOMPARE(count(item("bucket")), 1);
        QCOMPARE(count(item("water_bucket")), 1);
        QVERIFY(m_client->sounds.contains(u"qtcraft:item.bucket.fill"_s));

        // Flowing water cannot be taken.
        set(pool, u"water[level=3]"_s);
        use(pool, QcBlockFace::Up);
        QCOMPARE(nameAt(pool), u"qtcraft:water[falling=false,level=3]"_s);
        QCOMPARE(count(item("bucket")), 1);

        // Pouring places a source in front of the clicked face; the bucket comes back empty.
        for (int slot = 0; slot < m_client->stacks.size(); ++slot) {
            if (m_client->stacks[slot].item == item("water_bucket")) {
                send(QcMsg::SelectHotbarSlot{slot});
            }
        }
        const QcBlockPos ground = m_feet.offset(-2, -1, 0);
        use(ground, QcBlockFace::Up);
        QCOMPARE(nameAt(ground.offset(0, 1, 0)), u"qtcraft:water[falling=false,level=0]"_s);
        QCOMPARE(count(item("water_bucket")), 0);
        QCOMPARE(count(item("bucket")), 2);

        // The poured source spreads over the grass, towards the emptied pool four blocks east (the
        // nearest drop, E-103), not west.
        ticks(12);
        QCOMPARE(nameAt(ground.offset(1, 1, 0)), u"qtcraft:water[falling=false,level=1]"_s);
        QCOMPARE(nameAt(ground.offset(-1, 1, 0)), u"qtcraft:air"_s);

        // Lava the same way.
        const QcBlockPos lava = m_feet.offset(0, -1, 3);
        set(lava, u"lava"_s);
        hold("bucket");
        use(lava, QcBlockFace::Up);
        QCOMPARE(nameAt(lava), u"qtcraft:air"_s);
        QCOMPARE(count(item("lava_bucket")), 1);
    }

    void lavaHardensNextToWater()
    {
        join();
        const QcBlockPos source = m_feet.offset(3, 0, 0);
        const QcBlockPos flowing = m_feet.offset(-3, 0, 0);
        set(source, u"lava"_s);
        set(flowing, u"lava[level=2]"_s);
        set(flowing.offset(1, 0, 0), u"stone"_s);

        // Water put next to them by a command hardens both in the next tick.
        chat(u"/setblock %1 water"_s.arg(cmdPos(source.offset(0, 1, 0))));
        chat(u"/setblock %1 water"_s.arg(cmdPos(flowing.offset(0, 0, 1))));
        ticks(1);
        QCOMPARE(nameAt(source), u"qtcraft:obsidian"_s);
        QCOMPARE(nameAt(flowing), u"qtcraft:cobblestone"_s);
        QVERIFY(m_client->sounds.contains(u"qtcraft:liquid.hiss"_s));
    }

    void lavaHurtsBodiesAndBurnsItems()
    {
        join();
        const double before = m_client->health;
        set(m_feet, u"lava"_s);
        ticks(10);

        // Three points per hit, one hit per half second of invulnerability.
        QCOMPARE(m_client->health, before - 3.0);
        ticks(10);
        QCOMPARE(m_client->health, before - 6.0);
        set(m_feet, u"air"_s);

        // An item thrown into lava burns.
        const QcBlockPos pit = m_feet.offset(3, 0, 0);
        set(pit, u"lava"_s);
        m_server->items().spawnScattered({item("stick"), 3, 0}, {pit.x + 0.5, pit.y + 0.6, pit.z + 0.5}, 9000);
        QCOMPARE(dropped(item("stick")), 3);
        ticks(2);
        QCOMPARE(dropped(item("stick")), 0);

        // A lava pool slows a body like thick water.
        QCOMPARE(QcPhysics::liquidFactorAt(m_server->world(), pit), 0.45);
    }

    void contactDamageCoversChunkAndSectionBorders()
    {
        QcServerWorld world(m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        const QcContactDamage contact(*m_content);
        for (int x = -2; x <= 1; ++x) {
            for (int z = -2; z <= 1; ++z) {
                world.loadChunk({x, z});
            }
        }

        const QcBlockId lava = id(u"lava"_s);
        const QcBlockId flowing = id(u"lava[level=2]"_s);
        const double lavaDamage = m_content->block(lava).contactDamage;
        QVERIFY(lavaDamage > 0.0);
        const QcAabb onCorner{{-0.5, 63.5, -0.5}, {0.5, 64.5, 0.5}};
        QCOMPARE(contact.in(world, onCorner), 0.0);

        // The test terrain holds no lava.
        QVERIFY(!contact.isPossibleIn(world));

        // Lava on both sides of the chunk corner at (0, 0) and of the section border at y = 64.
        QVERIFY(world.setBlock({-1, 64, -1}, lava));
        QVERIFY(world.setBlock({0, 63, 15}, flowing));
        QVERIFY(world.setBlock({16, 70, 0}, lava));
        QVERIFY(world.setBlock({-17, 0, 5}, lava));
        QVERIFY(contact.isPossibleIn(world));

        // Independent reference: every overlapped cell looked up one by one.
        const auto expected = [&](const QcAabb &box) {
            double damage = 0.0;
            for (int x = int(std::floor(box.min.x + 1e-3)); x <= int(std::floor(box.max.x - 1e-3)); ++x) {
                for (int y = int(std::floor(box.min.y + 1e-3)); y <= int(std::floor(box.max.y - 1e-3)); ++y) {
                    for (int z = int(std::floor(box.min.z + 1e-3)); z <= int(std::floor(box.max.z - 1e-3)); ++z) {
                        const std::optional<QcBlockId> block = world.blockAt({x, y, z});
                        damage = std::max(damage, block ? m_content->block(*block).contactDamage : 0.0);
                    }
                }
            }
            return damage;
        };

        QCOMPARE(contact.in(world, onCorner), lavaDamage);

        // Touching a lava cell only on its face does not burn.
        QCOMPARE(contact.in(world, {{0.0, 64.0, -1.0}, {0.6, 65.8, -0.4}}), 0.0);
        QCOMPARE(contact.in(world, {{-0.02, 64.0, -1.0}, {0.6, 65.8, -0.4}}), lavaDamage);
        QCOMPARE(contact.in(world, {{-17.0, -2.0, 5.2}, {-16.5, 0.5, 5.8}}), lavaDamage);

        QRandomGenerator random(14);
        int burning = 0;
        for (int i = 0; i < 4000; ++i) {
            const QcVec3 min{random.bounded(40.0) - 20.0, random.bounded(76.0) - 2.0, random.bounded(40.0) - 20.0};
            const QcVec3 size{0.1 + random.bounded(1.4), 0.1 + random.bounded(1.9), 0.1 + random.bounded(1.4)};
            const QcAabb box{min, min + size};
            const double reference = expected(box);
            burning += reference > 0.0 ? 1 : 0;
            QCOMPARE(contact.in(world, box), reference);
        }
        QVERIFY(burning > 0);

        // Taken away again, nothing burns there any more.
        QVERIFY(world.setBlock({-1, 64, -1}, qcAirBlockId));
        QCOMPARE(contact.in(world, {{-1.0, 64.0, -1.0}, {-0.5, 64.5, -0.5}}), 0.0);

        // Lava in a chunk read from a save is found as well.
        auto stored = std::make_shared<QcChunk>(QcChunkPos{5, 5});
        stored->setBlock(3, 100, 3, lava);
        world.adoptStoredChunks({{QcChunkPos{5, 5}, stored}});
        QCOMPARE(contact.in(world, {{83.2, 100.2, 83.2}, {83.8, 101.0, 83.8}}), lavaDamage);
        QCOMPARE(contact.in(world, {{84.2, 100.2, 83.2}, {84.8, 101.0, 83.8}}), 0.0);
    }

    void generatedLavaPoolsStayStill()
    {
        QcServerConfig config;
        config.viewDistance = 1;
        auto generator = std::make_unique<SpawnAtGenerator>(m_content, 12345);
        const QcBlockId lava = id(u"lava"_s);

        // The first generated chunk with a lava pool.
        std::optional<QcChunkPos> pool;
        for (int i = 0; i < 64 && !pool; ++i) {
            const QcChunkPos pos{i % 8, i / 8};
            const auto chunk = generator->generate(pos);
            for (int s = 0; s < 4 && !pool; ++s) {
                if (chunk->section(s).palette().contains(lava)) {
                    pool = pos;
                }
            }
        }
        QVERIFY(pool);

        // A player standing above the pool keeps its chunks loaded.
        const int spawnX = pool->x * 16 + 8;
        const int spawnZ = pool->z * 16 + 8;
        generator->spawn = {spawnX, generator->column(spawnX, spawnZ).height + 1, spawnZ};
        config.operators = {u"Tester"_s};
        m_server = std::make_unique<QcServer>(config, m_content, std::move(generator));
        QVERIFY(m_server->openWorld().has_value());
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        m_client = std::make_unique<QcRawClient>(std::move(clientEnd));
        m_client->hello(u"Tester"_s);
        QcTestUtil::pump();

        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                m_server->world().loadChunk({pool->x + dx, pool->z + dz});
            }
        }

        QList<QcBlockPos> cells;
        for (int y = 0; y < 64; ++y) {
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    const QcBlockPos pos{pool->x * 16 + x, y, pool->z * 16 + z};
                    if (at(pos) == lava) {
                        cells.append(pos);
                    }
                }
            }
        }
        QVERIFY(!cells.isEmpty());

        // Loading plans no flow step. Even when every lava cell takes a step (as after a change next
        // to it), two lava step intervals later nothing has moved.
        ticks(2);
        QCOMPARE(m_server->liquids().plannedCount(), qsizetype(0));
        for (const QcBlockPos &pos : std::as_const(cells)) {
            QVERIFY(m_server->liquids().schedule(m_server->world(), pos, m_server->tickCount()));
        }

        ticks(2 * m_content->block(lava).flow.ticks);
        QCOMPARE(m_server->liquids().plannedCount(), qsizetype(0));
        for (const QcBlockPos &pos : std::as_const(cells)) {
            QCOMPARE(at(pos), lava);
        }
    }

    void plannedFlowSurvivesARestart()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config);

        const QcBlockPos source = m_feet.offset(0, 3, 0);
        chat(u"/setblock %1 water"_s.arg(cmdPos(source)));
        ticks(8);
        QVERIFY(m_server->liquids().plannedCount() > 0);

        QVERIFY(m_server->save().has_value());
        cleanup();
        join(config, false);
        for (int tick = 0; tick < 400 && (m_server->liquids().plannedCount() > 0 || m_server->pendingBlockUpdates() > 0); ++tick) {
            ticks(1);
        }

        // The flow finished after loading: it reached the floor below and spread seven blocks.
        QCOMPARE(nameAt(m_feet), u"qtcraft:water[falling=true,level=1]"_s);
        QCOMPARE(nameAt(m_feet.offset(5, 0, 0)), u"qtcraft:water[falling=false,level=4]"_s);
    }

    void torchesWashAwayLaddersStay()
    {
        join();

        const QcBlockPos torch = m_feet.offset(3, 0, 0);
        const QcBlockPos ladder = m_feet.offset(-3, 0, 0);
        set(torch, u"torch"_s);
        set(ladder.offset(-1, 0, 0), u"stone"_s);
        set(ladder.offset(1, 0, 0), u"stone"_s);
        set(ladder, u"ladder[attach=east]"_s);
        QCOMPARE(nameAt(ladder), u"qtcraft:ladder[attach=east]"_s);

        chat(u"/setblock %1 water"_s.arg(cmdPos(torch.offset(0, 0, 1))));
        chat(u"/setblock %1 water"_s.arg(cmdPos(ladder.offset(0, 0, 1))));
        ticks(20);
        QCOMPARE(nameAt(torch), u"qtcraft:water[falling=false,level=1]"_s);
        QCOMPARE(dropped(item("torch")) + count(item("torch")), 1);
        QCOMPARE(nameAt(ladder), u"qtcraft:ladder[attach=east]"_s);
    }

    void sandFallsAndLands()
    {
        join();
        const QcBlockPos high = m_feet.offset(2, 5, 0);
        chat(u"/setblock %1 sand"_s.arg(cmdPos(high)));
        ticks(1);
        QCOMPARE(nameAt(high), u"qtcraft:air"_s);
        QCOMPARE(m_server->fallingBlocks().all().size(), qsizetype(1));
        ticks(1);

        // The client sees a falling block entity of sand.
        const auto spawn = std::ranges::find(m_client->spawns, QcEntityKind::FallingBlock, &QcMsg::EntitySpawn::kind);
        QVERIFY(spawn != m_client->spawns.end());
        QCOMPARE(spawn->block, id(u"sand"_s));
        ticks(40);
        QCOMPARE(m_server->fallingBlocks().all().size(), qsizetype(0));
        QCOMPARE(nameAt(m_feet.offset(2, 0, 0)), u"qtcraft:sand"_s);

        // A column falls block by block and stacks up again.
        for (int dy = 3; dy <= 5; ++dy) {
            chat(u"/setblock %1 gravel"_s.arg(cmdPos(m_feet.offset(-2, dy, 0))));
        }
        ticks(60);
        for (int dy = 0; dy <= 2; ++dy) {
            QCOMPARE(nameAt(m_feet.offset(-2, dy, 0)), u"qtcraft:gravel"_s);
        }
        QCOMPARE(nameAt(m_feet.offset(-2, 3, 0)), u"qtcraft:air"_s);

        // Onto a torch: the sand drops as an item, the torch stays.
        set(m_feet.offset(0, 0, 3), u"torch"_s);
        chat(u"/setblock %1 sand"_s.arg(cmdPos(m_feet.offset(0, 4, 3))));
        ticks(40);
        QCOMPARE(nameAt(m_feet.offset(0, 0, 3)), u"qtcraft:torch[attach=floor]"_s);
        QCOMPARE(dropped(item("sand")), 1);

        // Into water: it sinks and replaces the water at the bottom.
        set(m_feet.offset(0, 0, -3), u"water"_s);
        set(m_feet.offset(0, 1, -3), u"water"_s);
        chat(u"/setblock %1 sand"_s.arg(cmdPos(m_feet.offset(0, 4, -3))));
        ticks(80);
        QCOMPARE(nameAt(m_feet.offset(0, 0, -3)), u"qtcraft:sand"_s);
    }

    void fallingBlocksDropInsteadOfBuryingBodies()
    {
        join();

        // Sand let go right above the player falls into the player's cell and drops as an item.
        chat(u"/setblock %1 sand"_s.arg(cmdPos(m_feet.offset(0, 5, 0))));
        ticks(40);
        QCOMPARE(m_server->fallingBlocks().all().size(), qsizetype(0));
        QCOMPARE(nameAt(m_feet), u"qtcraft:air"_s);
        QCOMPARE(nameAt(m_feet.offset(0, 1, 0)), u"qtcraft:air"_s);
        QCOMPARE(dropped(item("sand")) + count(item("sand")), 1);

        // Next to the player it lands as a block.
        chat(u"/setblock %1 sand"_s.arg(cmdPos(m_feet.offset(2, 5, 0))));
        ticks(40);
        QCOMPARE(nameAt(m_feet.offset(2, 0, 0)), u"qtcraft:sand"_s);

        // A pig standing across a cell border is only partly inside the landing cell; the sand still
        // drops. The pig is walled into a one block high trench of two cells.
        const QcBlockPos cell = m_feet.offset(-4, 0, 0);
        for (const QcBlockPos &wall : {cell.offset(-1, 0, 0), cell.offset(2, 0, 0), cell.offset(0, 0, -1), cell.offset(0, 0, 1),
                                       cell.offset(1, 0, -1), cell.offset(1, 0, 1), cell.offset(1, 1, 0)}) {
            set(wall, u"stone"_s);
        }
        const std::optional<int> pig = m_content->mobIndex(u"qtcraft:pig"_s);
        QVERIFY(pig);
        m_server->mobs().spawn(*pig, {cell.x + 1.0, double(cell.y), cell.z + 0.5}, 900001);
        const double sandBefore = dropped(item("sand")) + count(item("sand"));
        chat(u"/setblock %1 sand"_s.arg(cmdPos(cell.offset(0, 2, 0))));

        // The pig wanders, so its place is taken in the last tick the sand is still falling.
        double x = 0.0;
        bool fell = false;
        for (int tick = 0; tick < 20; ++tick) {
            const auto body = std::ranges::find(m_server->mobs().mobs(), quint32(900001), &QcCreature::id);
            QVERIFY(body != m_server->mobs().mobs().end());
            if (!m_server->fallingBlocks().all().isEmpty()) {
                x = body->motion.position.x;
                fell = true;
            }
            ticks(1);
        }
        QVERIFY(fell);
        QVERIFY2(x - 0.45 < cell.x + 1.0 && x + 0.45 > cell.x + 1.0, qPrintable(u"%1 %2"_s.arg(x).arg(cell.x)));
        QCOMPARE(nameAt(cell), u"qtcraft:air"_s);
        QCOMPARE(dropped(item("sand")) + count(item("sand")), sandBefore + 1);
    }

    void stillSourcesReactAcrossAFreshChunkBorder()
    {
        join();

        // Two stored chunks arrive together: a still lava source walled in stone at the east edge
        // of one, a still water source walled in stone at the west edge of the other.
        const QcChunkPos west{m_feet.chunk().x + 8, m_feet.chunk().z};
        const QcChunkPos east{west.x + 1, west.z};
        QVERIFY(!m_server->world().chunk(west) && !m_server->world().chunk(east));
        const QcBlockId stone = id(u"stone"_s);
        constexpr int y = 120;
        constexpr int z = 5;
        const auto walled = [&](QcChunkPos pos, int x, int inward, QcBlockId liquid) {
            std::shared_ptr<QcChunk> chunk = m_server->world().generator().generate(pos);
            chunk->setBlock(x, y, z, liquid);
            chunk->setBlock(x + inward, y, z, stone);
            chunk->setBlock(x, y, z - 1, stone);
            chunk->setBlock(x, y, z + 1, stone);
            chunk->setBlock(x, y - 1, z, stone);
            return chunk;
        };
        m_server->world().adoptStoredChunks(
            {{west, walled(west, QcWorld::chunkSize - 1, -1, id(u"lava"_s))}, {east, walled(east, 0, 1, id(u"water"_s))}});
        const QcBlockPos lava{east.x * QcWorld::chunkSize - 1, y, west.z * QcWorld::chunkSize + z};
        ticks(2);
        QCOMPARE(nameAt(lava), u"qtcraft:obsidian"_s);
        QCOMPARE(nameAt(lava.offset(1, 0, 0)), u"qtcraft:water[falling=false,level=0]"_s);
    }

    void fallingBlocksSurviveARestart()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config);

        chat(u"/setblock %1 sand"_s.arg(cmdPos(m_feet.offset(2, 8, 0))));
        ticks(2);
        QCOMPARE(m_server->fallingBlocks().all().size(), qsizetype(1));
        const double y = m_server->fallingBlocks().all().constFirst().motion.position.y;
        QVERIFY(y < m_feet.y + 8.0 && y > m_feet.y + 1.0);

        QVERIFY(m_server->save().has_value());
        cleanup();
        join(config, false);

        QCOMPARE(m_server->fallingBlocks().all().size(), qsizetype(1));
        QCOMPARE(m_server->fallingBlocks().all().constFirst().block, id(u"sand"_s));
        ticks(40);
        QCOMPARE(nameAt(m_feet.offset(2, 0, 0)), u"qtcraft:sand"_s);
    }

    void plantsNeedTheirGround()
    {
        join();
        face(0.0f);
        hold("dandelion", 4);

        // Not on stone.
        set(m_feet.offset(2, -1, 0), u"stone"_s);
        use(m_feet.offset(2, -1, 0), QcBlockFace::Up);
        QCOMPARE(nameAt(m_feet.offset(2, 0, 0)), u"qtcraft:air"_s);

        // On grass; it breaks with its drop when the grass goes.
        use(m_feet.offset(-2, -1, 0), QcBlockFace::Up);
        QCOMPARE(nameAt(m_feet.offset(-2, 0, 0)), u"qtcraft:dandelion"_s);
        chat(u"/setblock %1 air"_s.arg(cmdPos(m_feet.offset(-2, -1, 0))));
        ticks(2);
        QCOMPARE(nameAt(m_feet.offset(-2, 0, 0)), u"qtcraft:air"_s);
        QCOMPARE(dropped(item("dandelion")), 1);

        // Sugar cane needs water next to its sand, but not on top of another cane.
        hold("sugar_cane", 4);
        const QcBlockPos sand = m_feet.offset(0, -1, 3);
        set(sand, u"sand"_s);
        use(sand, QcBlockFace::Up);
        QCOMPARE(nameAt(sand.offset(0, 1, 0)), u"qtcraft:air"_s);
        set(sand.offset(1, 0, 0), u"water"_s);
        use(sand, QcBlockFace::Up);
        QCOMPARE(nameAt(sand.offset(0, 1, 0)), u"qtcraft:sugar_cane"_s);
        use(sand.offset(0, 1, 0), QcBlockFace::Up);
        QCOMPARE(nameAt(sand.offset(0, 2, 0)), u"qtcraft:sugar_cane"_s);

        // Breaking the lower cane brings down the upper one.
        chat(u"/setblock %1 air"_s.arg(cmdPos(sand.offset(0, 1, 0))));
        ticks(2);
        QCOMPARE(nameAt(sand.offset(0, 2, 0)), u"qtcraft:air"_s);

        // Tall grass is replaced by a placed block; flowing water washes flowers away.
        set(m_feet.offset(2, 0, -2), u"tall_grass"_s);
        hold("stone", 2);
        use(m_feet.offset(2, -1, -2), QcBlockFace::Up);
        QCOMPARE(nameAt(m_feet.offset(2, 0, -2)), u"qtcraft:stone"_s);

        // Clicking the tall grass itself puts the block into its cell.
        set(m_feet.offset(-2, 0, 2), u"tall_grass"_s);
        use(m_feet.offset(-2, 0, 2), QcBlockFace::East);
        QCOMPARE(nameAt(m_feet.offset(-2, 0, 2)), u"qtcraft:stone"_s);
        QCOMPARE(nameAt(m_feet.offset(-1, 0, 2)), u"qtcraft:air"_s);
        // Walls on the other sides make the flower the only way for the water, whatever drops lie near.
        set(m_feet.offset(-3, 0, -3), u"poppy"_s);
        for (const QcBlockPos wall : {m_feet.offset(-5, 0, -3), m_feet.offset(-4, 0, -2), m_feet.offset(-4, 0, -4)}) {
            set(wall, u"stone"_s);
        }
        chat(u"/setblock %1 water"_s.arg(cmdPos(m_feet.offset(-4, 0, -3))));
        ticks(12);
        QCOMPARE(nameAt(m_feet.offset(-3, 0, -3)), u"qtcraft:water[falling=false,level=1]"_s);
        QCOMPARE(dropped(item("poppy")), 1);
    }

    void firefliesAreCaughtWithABottle()
    {
        join();

        // Fireflies only stay at night.
        chat(u"/time set night"_s);
        const int type = *m_content->mobIndex(u"qtcraft:firefly"_s);
        const quint32 firefly = m_server->mobs().spawn(type, m_client->motion.position + QcVec3{1.5, 1.5, 0.0}, 9000);

        // Wrong item: nothing happens.
        hold("stick");
        send(QcMsg::UseEntity{firefly});
        QVERIFY(m_server->mobs().find(firefly));
        hold("glass_bottle", 2);
        send(QcMsg::UseEntity{firefly});
        QVERIFY(!m_server->mobs().find(firefly));
        QCOMPARE(count(item("glass_bottle")), 1);
        QCOMPARE(count(item("firefly_bottle")), 1);
        QVERIFY(m_client->sounds.contains(u"qtcraft:item.bottle.catch"_s));

        // Out of reach a creature cannot be caught.
        const quint32 far = m_server->mobs().spawn(type, m_client->motion.position + QcVec3{12.0, 2.0, 0.0}, 9100);
        send(QcMsg::UseEntity{far});
        QVERIFY(m_server->mobs().find(far));
    }

    void firefliesSpawnAtNightInSwampsOnly()
    {
        const auto world = [&](bool swamp) {
            auto made = std::make_unique<QcServerWorld>(m_content, swamp ? std::make_unique<SwampGenerator>(m_content)
                                                                         : std::make_unique<QcTestTerrainGenerator>(m_content));
            for (int x = -4; x <= 4; ++x) {
                for (int z = -4; z <= 4; ++z) {
                    made->loadChunk({x, z});
                }
            }
            return made;
        };

        const int firefly = *m_content->mobIndex(u"qtcraft:firefly"_s);
        const QcMobDefinition &definition = m_content->mobs()[firefly];
        quint32 next = 100;
        const QcVec3 player{0.5, 70.0, 0.5};
        const auto countFireflies = [&](const QcMobManager &mobs) {
            return std::ranges::count(mobs.mobs(), firefly, &QcCreature::type);
        };

        auto swamp = world(true);
        QcMobManager mobs(m_content, 7);
        int most = 0;
        for (int tick = 0; tick < 20 * 60; ++tick) {
            mobs.tick(*swamp, {{7, player, true}}, midnight, 0.05, [&next] { return next++; });
            most = std::max(most, int(countFireflies(mobs)));
            // Fireflies fly: never on the ground.
            for (const QcCreature &mob : mobs.mobs()) {
                if (mob.type == firefly) {
                    QVERIFY(!mob.motion.onGround);
                }
            }
        }

        QVERIFY2(most > 0, "no firefly spawned in the swamp at night");
        QVERIFY(most <= definition.maxCount);
        QCOMPARE(definition.maxCount, 8);

        // At daybreak they are gone, even close to the player.
        mobs.tick(*swamp, {{7, player, true}}, noon, 0.05, [&next] { return next++; });
        QCOMPARE(countFireflies(mobs), 0);

        // Outside a swamp, or in daylight, none appear.
        auto plain = world(false);
        QcMobManager elsewhere(m_content, 7);
        QcMobManager daytime(m_content, 7);
        for (int tick = 0; tick < 20 * 60; ++tick) {
            elsewhere.tick(*plain, {{7, player, true}}, midnight, 0.05, [&next] { return next++; });
            daytime.tick(*swamp, {{7, player, true}}, noon, 0.05, [&next] { return next++; });
        }
        QCOMPARE(countFireflies(elsewhere), 0);
        QCOMPARE(countFireflies(daytime), 0);
    }

    void mineLampChainLightAndSupport()
    {
        // Clay balls -> unfired shell (crafting table) -> fired shell (furnace) + firefly bottle -> lamp.
        const QcItemId clay = item("clay_ball");
        QList<QcItemStack> grid(9);
        for (const int cell : {1, 3, 5, 7}) {
            grid[cell] = {clay, 1, 0};
        }
        const std::optional<qsizetype> shell = QcCrafting::match(*m_content, grid, 3);
        QVERIFY(shell);
        QCOMPARE(m_content->recipes()[*shell].resultItem, u"qtcraft:unfired_lamp_shell"_s);

        QcTestWorld world(*m_content);
        const QcBlockPos furnacePos{0, 64, 0};
        world.set(furnacePos, *m_content->blockId(u"qtcraft:furnace"_s));
        QcTestMachines machines(m_content);
        QVERIFY(machines.add(furnacePos, *m_content->blockId(u"qtcraft:furnace"_s)));
        QcBlockEntities::Machine &furnace = *machines.find(furnacePos);
        furnace.stacks[QcMachineRules::inputSlot] = {item("unfired_lamp_shell"), 1, 0};
        furnace.stacks[QcMachineRules::fuelSlot] = {item("coal"), 1, 0};
        for (int tick = 0; tick < 20 * 12; ++tick) {
            machines.tick(world, 0.05);
        }

        QCOMPARE(machines.find(furnacePos)->stacks[QcMachineRules::outputSlot], (QcItemStack{item("fired_lamp_shell"), 1, 0}));

        QList<QcItemStack> small(4);
        small[0] = {item("firefly_bottle"), 1, 0};
        small[3] = {item("fired_lamp_shell"), 1, 0};
        const std::optional<qsizetype> lamp = QcCrafting::match(*m_content, small, 2);
        QVERIFY(lamp);
        QCOMPARE(m_content->recipes()[*lamp].resultItem, u"qtcraft:mine_lamp"_s);

        // Placed under a ceiling it hangs there and lights the room a little less than a torch.
        join();
        face(0.0f, 60.0f);
        const QcBlockPos ceiling = m_feet.offset(0, 4, -2);
        set(ceiling, u"stone"_s);
        hold("mine_lamp", 3);
        use(ceiling, QcBlockFace::Down);
        QCOMPARE(nameAt(ceiling.offset(0, -1, 0)), u"qtcraft:mine_lamp[attach=ceiling]"_s);
        QCOMPARE(QcLightVolume::blockLightAt(m_server->world(), ceiling.offset(0, -1, 0)), 12);
        QCOMPARE(QcLightVolume::blockLightAt(m_server->world(), ceiling.offset(0, -3, 0)), 10);
        QCOMPARE(m_content->block(*m_content->blockId(u"qtcraft:torch"_s)).lightLevel, 14);

        // On a wall and on the floor too.
        const QcBlockPos wall = m_feet.offset(2, 1, -3);
        set(wall, u"stone"_s);
        use(wall, QcBlockFace::South);
        QCOMPARE(nameAt(wall.offset(0, 0, 1)), u"qtcraft:mine_lamp[attach=north]"_s);
        use(m_feet.offset(-2, -1, -1), QcBlockFace::Up);
        QCOMPARE(nameAt(m_feet.offset(-2, 0, -1)), u"qtcraft:mine_lamp[attach=floor]"_s);

        // Without its ceiling it falls off as an item.
        chat(u"/setblock %1 air"_s.arg(cmdPos(ceiling)));
        ticks(2);
        QCOMPARE(nameAt(ceiling.offset(0, -1, 0)), u"qtcraft:air"_s);
        QCOMPARE(dropped(item("mine_lamp")), 1);
    }

    void moreMaterialsWoolDyesCarpetsAndSnow()
    {
        const auto crafted = [&](const QList<std::pair<int, const char *>> &cells, int size) {
            QList<QcItemStack> grid(qsizetype(size) * size);
            for (const auto &[cell, name] : cells) {
                grid[cell] = {item(name), 1, 0};
            }
            const std::optional<qsizetype> recipe = QcCrafting::match(*m_content, grid, size);
            return recipe ? m_content->recipes()[*recipe].resultItem : QString();
        };

        // Stone bricks from four stone, polished stone from the furnace; both get building parts.
        QCOMPARE(crafted({{0, "stone"}, {1, "stone"}, {2, "stone"}, {3, "stone"}}, 2), u"qtcraft:stone_bricks"_s);
        QCOMPARE(m_content->smeltingRecipe(u"smelting"_s, u"qtcraft:stone"_s)->resultItem, u"qtcraft:polished_stone"_s);
        QVERIFY(m_content->blockId(u"qtcraft:stone_bricks_wall"_s));
        QVERIFY(m_content->blockId(u"qtcraft:polished_stone_slab"_s));
        QVERIFY(!m_content->blockId(u"qtcraft:polished_stone_wall"_s));

        // Dyes from flowers and coal chunks, mixed dyes, coloured wool from any wool and a dye.
        QCOMPARE(crafted({{0, "poppy"}}, 2), u"qtcraft:red_dye"_s);
        QCOMPARE(crafted({{0, "cornflower"}}, 2), u"qtcraft:blue_dye"_s);
        QCOMPARE(crafted({{0, "coal_chunk"}}, 2), u"qtcraft:black_dye"_s);
        QCOMPARE(crafted({{0, "red_dye"}, {1, "blue_dye"}}, 2), u"qtcraft:purple_dye"_s);
        QCOMPARE(crafted({{0, "white_wool"}, {3, "purple_dye"}}, 2), u"qtcraft:purple_wool"_s);
        QCOMPARE(crafted({{0, "red_wool"}, {1, "yellow_dye"}}, 2), u"qtcraft:yellow_wool"_s);
        QCOMPARE(m_content->smeltingRecipe(u"smelting"_s, u"qtcraft:cactus"_s)->resultItem, u"qtcraft:green_dye"_s);

        // Carpets come from two wool, are thin and need a floor.
        QCOMPARE(crafted({{0, "green_wool"}, {1, "green_wool"}}, 2), u"qtcraft:green_wool_carpet"_s);
        const QcBlockId carpet = id(u"green_wool_carpet"_s);
        QCOMPARE(m_content->state(carpet).boxes.size(), qsizetype(1));
        QCOMPARE(m_content->state(carpet).boxes[0].max.y, 0.0625);
        QCOMPARE(m_content->block(carpet).support, QcBlockSupport::Below);

        // Sheep drop white wool.
        const QcMobDefinition &sheep = m_content->mobs()[*m_content->mobIndex(u"qtcraft:sheep"_s)];
        QCOMPARE(sheep.drops.first().item, u"qtcraft:white_wool"_s);

        // Snow blocks from four snowballs (decision E-427).
        QCOMPARE(crafted({{0, "snowball"}, {1, "snowball"}, {2, "snowball"}, {3, "snowball"}}, 2), u"qtcraft:snow_block"_s);

        // Snow layers from snow blocks; on the server they need ground and break without it.
        QCOMPARE(crafted({{0, "snow_block"}, {1, "snow_block"}, {2, "snow_block"}}, 3), u"qtcraft:snow_layer"_s);
        join();
        hold("snow_layer", 2);
        use(m_feet.offset(2, -1, 0), QcBlockFace::Up);
        QCOMPARE(nameAt(m_feet.offset(2, 0, 0)), u"qtcraft:snow_layer"_s);
        chat(u"/setblock %1 air"_s.arg(cmdPos(m_feet.offset(2, -1, 0))));
        ticks(2);
        QCOMPARE(nameAt(m_feet.offset(2, 0, 0)), u"qtcraft:air"_s);
        // A layer drops a snowball (decision E-427).
        QCOMPARE(dropped(item("snowball")), 1);
    }

    void generatorPlacesVegetationOnItsGround()
    {
        QStringList warnings;
        const QcTerrainGenerator generator(m_content, 42, &warnings);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));
        QHash<QString, int> found;
        const QStringList plants = {u"qtcraft:tall_grass"_s, u"qtcraft:dandelion"_s, u"qtcraft:poppy"_s, u"qtcraft:cornflower"_s,
                                    u"qtcraft:brown_mushroom"_s, u"qtcraft:red_mushroom"_s, u"qtcraft:sugar_cane"_s};

        for (int cx = -12; cx <= 12; cx += 2) {
            for (int cz = -12; cz <= 12; cz += 2) {
                const std::shared_ptr<QcChunk> chunk = generator.generate({cx, cz});
                const std::shared_ptr<QcChunk> again = generator.generate({cx, cz});
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    for (int z = 0; z < QcWorld::chunkSize; ++z) {
                        for (int y = 1; y < QcWorld::worldHeight; ++y) {
                            const QcBlockId block = chunk->block(x, y, z);
                            QCOMPARE(again->block(x, y, z), block);
                            const QString &name = m_content->block(block).name;
                            if (!plants.contains(name)) {
                                continue;
                            }
                            ++found[name];

                            // Every plant stands on ground its rules allow (cane also on cane).
                            const QcBlockDefinition &definition = m_content->block(block);
                            const QString ground = m_content->block(chunk->block(x, y - 1, z)).name;
                            QVERIFY2(definition.groundBlocks.isEmpty() ? m_content->block(chunk->block(x, y - 1, z)).solid
                                                                       : definition.groundBlocks.contains(ground),
                                     qPrintable(name + u" on "_s + ground));
                        }
                    }
                }
            }
        }

        QVERIFY2(found.value(u"qtcraft:tall_grass"_s) > 100, qPrintable(QString::number(found.value(u"qtcraft:tall_grass"_s))));
        int flowers = 0;
        for (const QString &flower : {u"qtcraft:dandelion"_s, u"qtcraft:poppy"_s, u"qtcraft:cornflower"_s}) {
            flowers += found.value(flower);
        }
        QVERIFY(flowers > 0);
    }
};

QTEST_GUILESS_MAIN(TestWorldDetails)
#include "test_world_details.moc"
