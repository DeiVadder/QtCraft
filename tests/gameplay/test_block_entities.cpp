#include "common/physics/QcPhysics.h"
#include "QcMachineFixture.h"
#include "QcTestUtil.h"
#include "server/blockentities/QcBlockEntityChecks.h"

#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

namespace {

// Test terrain with an empty chest in the middle of every chunk, as structures place them.
class ChestTerrainGenerator : public QcTestTerrainGenerator
{
public:
    explicit ChestTerrainGenerator(const std::shared_ptr<const QcContentRegistry> &content)
        : QcTestTerrainGenerator(content)
        , m_chest(*content->blockId(u"qtcraft:chest"_s))
    {
    }

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override
    {
        std::shared_ptr<QcChunk> chunk = QcTestTerrainGenerator::generate(pos);
        chunk->setBlock(8, 100, 8, m_chest);
        return chunk;
    }

private:
    QcBlockId m_chest;
};

// The base content with a sapling that has two states, as a mod may define one.
std::shared_ptr<const QcContentRegistry> contentWithStagedSapling()
{
    std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
    QcBlockDefinition staged = content->block(*content->blockId(u"qtcraft:oak_sapling"_s));
    staged.name = u"mod:staged_sapling"_s;
    staged.properties = {{u"stage"_s, {u"0"_s, u"1"_s}}};
    if (!content->registerBlock(staged)) {
        QcTestUtil::failCase(QStringLiteral("cannot register mod:staged_sapling"));
    }
    return content;
}

} // namespace

// The block-entity register (stage 31.1, decision E-357) on a running server: entries follow their
// block wherever it is set, pistons move everything of a cell with it (also its scheduled tick,
// across chunk borders and through a save in the middle of the move), a regenerated chunk loses
// exactly its own entries, and the central checks find broken rules.
class TestBlockEntities : public QcMachineFixture
{
    Q_OBJECT

private:
    QcBlockEntities &entities() const { return m_server->blockEntities(); }

    void rulesHold(const char *when) const
    {
        const QStringList problems = QcBlockEntityChecks::problems(m_server->world());
        QVERIFY2(problems.isEmpty(), qPrintable(QString::fromLatin1(when) + u": "_s + problems.join(u"; "_s)));
    }

    // Moves the area so that a chunk border runs between local 6 and 7 along both axes.
    void areaAcrossAChunkBorder(QcServerConfig config = {})
    {
        buildArea(std::move(config));
        const QcBlockPos corner = m_feet.offset(-areaSize / 2, 12, -areaSize / 2);
        clearArea({qcFloorDiv(corner.x, QcWorld::chunkSize) * QcWorld::chunkSize + 9, corner.y,
                   qcFloorDiv(corner.z, QcWorld::chunkSize) * QcWorld::chunkSize + 9});
        QVERIFY(at(6, 0, 6).chunk() != at(7, 0, 6).chunk());
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void entriesFollowTheirBlock()
    {
        buildArea();
        put(2, 0, 2, u"chest[facing=south]"_s);
        QCOMPARE(entities().container(at(2, 0, 2))->stacks.size(), 27);
        put(4, 0, 2, u"furnace[facing=south]"_s);
        QVERIFY(entities().machine(at(4, 0, 2)));
        rulesHold("placed");

        // A hopper in the chest's place keeps five slots and spills the rest with the next tick.
        fill(2, 0, 2, {stack("qtcraft:dirt", 1), stack("qtcraft:dirt", 1), stack("qtcraft:dirt", 1), stack("qtcraft:dirt", 1),
                       stack("qtcraft:dirt", 1), stack("qtcraft:dirt", 1), stack("qtcraft:dirt", 1)});
        put(2, 0, 2, u"signal_hopper[facing=down]"_s);
        QCOMPARE(entities().container(at(2, 0, 2))->stacks.size(), 5);
        ticks(1);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 2);
        rulesHold("resized");

        // Stone takes container and machine away; their contents drop.
        fill(4, 0, 2, {stack("qtcraft:raw_iron", 3)});
        put(2, 0, 2, u"stone"_s);
        put(4, 0, 2, u"stone"_s);
        QVERIFY(!entities().container(at(2, 0, 2)));
        QVERIFY(!entities().machine(at(4, 0, 2)));
        ticks(1);
        QCOMPARE(everywhere("qtcraft:dirt"), 7);
        QCOMPARE(everywhere("qtcraft:raw_iron"), 3);
        rulesHold("removed");
    }

    void saplingTimersKeepTheirOrderThroughASave()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        buildArea(config);
        // Planted by commands, which set blocks like players do.
        const QList<QcBlockPos> planted{at(9, 0, 2), at(2, 0, 2), at(5, 0, 9)};
        for (const QcBlockPos &pos : planted) {
            put(pos.x - m_origin.x, -1, pos.z - m_origin.z, u"grass_block"_s);
            chat(u"/setblock %1 %2 %3 oak_sapling"_s.arg(pos.x).arg(pos.y).arg(pos.z));
        }
        QList<QcBlockPos> order;
        const QList<QcBlockEntities::Sapling> saplings = entities().saplingsInOrder();
        for (const QcBlockEntities::Sapling &sapling : saplings) {
            order.append(sapling.pos);
        }
        QCOMPARE(order, planted);

        // Replaced by stone, a sapling's timer ends.
        chat(u"/setblock %1 %2 %3 stone"_s.arg(planted[1].x).arg(planted[1].y).arg(planted[1].z));
        QVERIFY(!entities().sapling(planted[1]));
        const double remaining = entities().sapling(planted[2])->remaining;
        QVERIFY(m_server->save().has_value());
        cleanup();
        join(config);
        order.clear();
        const QList<QcBlockEntities::Sapling> reopened = entities().saplingsInOrder();
        for (const QcBlockEntities::Sapling &sapling : reopened) {
            order.append(sapling.pos);
        }
        QCOMPARE(order, (QList<QcBlockPos>{planted[0], planted[2]}));
        // Joining ran two ticks of growth.
        QCOMPARE(entities().sapling(planted[2])->remaining, remaining - 2 * QcPhysics::tickDuration);
    }

    void scheduledTicksMoveWithTheirBlock()
    {
        // Two lamps on one lever wait 4 ticks before they go dark once it is switched off; one is
        // pushed meanwhile and still goes dark in the same tick as the other, because its scheduled
        // tick moved with it to its new cell.
        buildArea();
        put(1, 0, 5, u"lever[attach=floor]"_s);
        put(0, 0, 5, u"signal_lamp"_s);
        put(2, 0, 5, u"signal_lamp"_s);
        put(2, 0, 3, u"lever[attach=floor]"_s);
        put(2, 0, 4, u"signal_piston[facing=south]"_s);
        flip(1, 0, 5);
        ticks(3);
        QVERIFY(isBlock(0, 0, 5, "signal_lamp_lit") && isBlock(2, 0, 5, "signal_lamp_lit"));
        flip(1, 0, 5);
        ticks(1);
        QCOMPARE(entities().schedule().size(), 2);
        flip(2, 0, 3);
        ticks(1);
        QVERIFY(isBlock(2, 0, 6, "signal_lamp_lit"));
        QVERIFY(entities().schedule().contains(at(2, 0, 6)));
        for (int tick = 0; tick < 8; ++tick) {
            const bool still = isBlock(0, 0, 5, "signal_lamp_lit");
            const bool pushed = isBlock(2, 0, 6, "signal_lamp_lit");
            QCOMPARE(pushed, still);
            ticks(1);
        }
        QVERIFY(isBlock(0, 0, 5, "signal_lamp") && isBlock(2, 0, 6, "signal_lamp"));
        rulesHold("after the lamps went dark");
    }

    void saveInTheMiddleOfAMoveAcrossAChunkBorder_data()
    {
        QTest::addColumn<int>("turns");
        for (int turns = 0; turns < 4; ++turns) {
            QTest::addRow("turns %d", turns) << turns;
        }
    }

    void saveInTheMiddleOfAMoveAcrossAChunkBorder()
    {
        QFETCH(int, turns);
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        m_turns = turns;
        areaAcrossAChunkBorder(config);
        put(2, 0, 6, u"lever[attach=floor]"_s);
        put(3, 0, 6, u"sticky_signal_piston[facing=east]"_s);
        put(4, 0, 6, u"chest[facing=south]"_s);
        put(5, 0, 6, u"furnace[facing=south]"_s);
        put(6, 0, 6, u"signal_hopper[facing=down]"_s);
        put(7, 0, 6, u"signal_dispenser[facing=up]"_s);
        fill(4, 0, 6, {stack("qtcraft:diamond", 5)});
        fill(5, 0, 6, {stack("qtcraft:raw_iron", 4)});
        fill(6, 0, 6, {stack("qtcraft:dirt", 3)});
        fill(7, 0, 6, {stack("qtcraft:stick", 9)});
        ticks(2);
        flip(2, 0, 6);
        ticks(1);
        // The blocks stand in their new cells while the clients still see them slide.
        QVERIFY(!m_server->movingBlocks().all().isEmpty());
        QVERIFY(m_server->save().has_value());
        const QcBlockPos origin = m_origin;
        cleanup();
        join(config);
        m_origin = origin;
        m_turns = turns;
        QCOMPARE(countIn(5, 0, 6, "qtcraft:diamond"), 5);
        QCOMPARE(countIn(6, 0, 6, "qtcraft:raw_iron"), 4);
        QCOMPARE(countIn(7, 0, 6, "qtcraft:dirt"), 3);
        QCOMPARE(countIn(8, 0, 6, "qtcraft:stick"), 9);
        QCOMPARE(everywhere("qtcraft:diamond"), 5);
        QCOMPARE(everywhere("qtcraft:raw_iron"), 4);
        QCOMPARE(everywhere("qtcraft:dirt"), 3);
        QCOMPARE(everywhere("qtcraft:stick"), 9);
        rulesHold("reopened");
    }

    void regenerationDropsExactlyTheChunksBlockEntities()
    {
        areaAcrossAChunkBorder();
        // The same set on both sides of the border: chest, furnace, sapling, burnt-out torch memory
        // and a scheduled tick.
        for (const int x : {2, 10}) {
            put(x, 0, 1, u"chest[facing=south]"_s);
            fill(x, 0, 1, {stack("qtcraft:diamond", 2)});
            put(x, 0, 2, u"furnace[facing=south]"_s);
            fill(x, 0, 2, {stack("qtcraft:raw_iron", 1)});
            put(x, -1, 3, u"grass_block"_s);
            chat(u"/setblock %1 %2 %3 oak_sapling"_s.arg(at(x, 0, 3).x).arg(at(x, 0, 3).y).arg(at(x, 0, 3).z));
            put(x, 0, 4, u"signal_torch"_s);
            entities().memoryAt(at(x, 0, 4)).burntOutUntil = m_server->tickCount() + 500;
            entities().schedule().add(at(x, 0, 5), m_server->tickCount() + 500, QcSignalSchedule::Priority::Release);
        }
        rulesHold("set up");
        const QcChunkPos regenerated = at(2, 0, 1).chunk();
        const QcChunkPos kept = at(10, 0, 1).chunk();
        QVERIFY(regenerated != kept);
        for (int z = 1; z <= 5; ++z) {
            QCOMPARE(at(2, 0, z).chunk(), regenerated);
            QCOMPARE(at(10, 0, z).chunk(), kept);
        }
        const qsizetype itemsBefore = m_server->items().items().size();

        const QcBlockPos corner{regenerated.x * QcWorld::chunkSize, 0, regenerated.z * QcWorld::chunkSize};
        chat(u"/regenerate %1 %2"_s.arg(corner.x).arg(corner.z));
        chat(u"/confirm"_s);
        ticks(1);

        QVERIFY(!entities().chunk(regenerated));
        QVERIFY(entities().schedule().entriesIn(regenerated).isEmpty());
        // Contents of a regenerated chunk are lost, not dropped (decision E-246).
        QCOMPARE(m_server->items().items().size(), itemsBefore);
        QCOMPARE(countIn(10, 0, 1, "qtcraft:diamond"), 2);
        QCOMPARE(countIn(10, 0, 2, "qtcraft:raw_iron"), 1);
        QVERIFY(entities().sapling(at(10, 0, 3)));
        QVERIFY(entities().memory(at(10, 0, 4)));
        QVERIFY(entities().schedule().contains(at(10, 0, 5)));
        rulesHold("regenerated");
    }

    void entriesOfUnloadedChunksStayBounded()
    {
        // Generated chests get their entry when their chunk arrives and lose it, empty, when it
        // unloads; a filled one keeps it. Loading and unloading again and again does not pile up.
        QcServerWorld world(m_content, std::make_unique<ChestTerrainGenerator>(m_content));
        const QcBlockEntities &kept = world.blockEntities();
        for (int round = 0; round < 3; ++round) {
            for (int x = 0; x < 5; ++x) {
                for (int z = 0; z < 5; ++z) {
                    world.loadChunk({x, z});
                }
            }
            QCOMPARE(kept.containerCount(), 25);
            if (round == 0) {
                world.blockEntities().container({8, 100, 8})->stacks[0] = {item("qtcraft:diamond"), 1, 0};
            }
            for (int x = 0; x < 5; ++x) {
                for (int z = 0; z < 5; ++z) {
                    world.unloadChunk({x, z});
                }
            }
            QCOMPARE(kept.containerCount(), 1);
            QCOMPARE(kept.chunks().size(), 1);
        }
    }

    void saplingStatesKeepTheirTimer()
    {
        // A sapling whose state changes (a mod's growth stage) keeps its timer; another block ends it.
        m_content = contentWithStagedSapling();
        buildArea();
        put(4, -1, 4, u"grass_block"_s);
        const QcBlockPos pos = at(4, 0, 4);
        chat(u"/setblock %1 %2 %3 mod:staged_sapling[stage=0]"_s.arg(pos.x).arg(pos.y).arg(pos.z));
        QVERIFY(entities().sapling(pos));
        const quint64 order = entities().sapling(pos)->order;
        chat(u"/setblock %1 %2 %3 mod:staged_sapling[stage=1]"_s.arg(pos.x).arg(pos.y).arg(pos.z));
        QVERIFY(entities().sapling(pos));
        QCOMPARE(entities().sapling(pos)->order, order);
        chat(u"/setblock %1 %2 %3 stone"_s.arg(pos.x).arg(pos.y).arg(pos.z));
        QVERIFY(!entities().sapling(pos));
        rulesHold("after the stages");
        m_content = QcTestUtil::loadContent();
    }

    void checksFindBrokenRules()
    {
        // The checks are only worth something if they fail: each broken rule is found.
        buildArea();
        rulesHold("clean");
        const QcBlockPos air = at(3, 0, 3);
        QVERIFY(entities().putContainer({air, QList<QcItemStack>(27), 0}));
        QVERIFY(QcBlockEntityChecks::problems(m_server->world()).join(u'\n').contains(u"container at"_s));
        put(3, 0, 3, u"chest[facing=south]"_s);
        rulesHold("the chest fits now");

        entities().memoryAt(at(5, 0, 5)).powered = true;
        QVERIFY(QcBlockEntityChecks::problems(m_server->world()).join(u'\n').contains(u"memory at"_s));
        entities().memoryAt(at(5, 0, 5)).powered = false;
        entities().prune(m_server->tickCount(), 60, 2);
        rulesHold("memory pruned");

        // A chest block without its container (set behind the register's back, here by forgetting
        // the chunk's entries) is found too.
        const QcChunkPos chunk = at(3, 0, 3).chunk();
        entities().forgetChunk(chunk);
        QVERIFY(QcBlockEntityChecks::problems(m_server->world()).join(u'\n').contains(u"container block at"_s));

        // A chunk that enters the world (generated, read, loaded again) gets the containers and
        // machines of its blocks.
        m_server->world().adoptStoredChunks({{chunk, m_server->world().takeChunk(chunk)}});
        const QcBlockEntities::Container *arrived = entities().container(at(3, 0, 3));
        QVERIFY(arrived);
        QCOMPARE(arrived->stacks.size(), 27);
        rulesHold("arrived again");
    }
};

QTEST_GUILESS_MAIN(TestBlockEntities)
#include "test_block_entities.moc"
