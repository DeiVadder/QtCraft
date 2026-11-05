// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/QcLoadingProgress.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientWorld.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcChunk.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

using Stage = QcLoadingProgress::Stage;
using Kind = QcLoadingProgress::Kind;
using Area = QcLoadingProgress::Area;

constexpr int timeoutMs = 10000;

std::shared_ptr<QcContentRegistry> stoneContent()
{
    auto content = std::make_shared<QcContentRegistry>();
    QcBlockDefinition stone;
    stone.name = u"t:stone"_s;
    stone.faceTextures.fill(stone.name);
    if (!content->registerBlock(stone)) {
        qFatal("cannot register the test block");
    }
    return content;
}

std::shared_ptr<QcChunk> stoneChunk(QcChunkPos pos)
{
    auto chunk = std::make_shared<QcChunk>(pos);
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            chunk->setBlock(x, 0, z, 1);
        }
    }
    return chunk;
}

} // namespace

// The state of a world start for the loading screen (decision E-943): the stages in their order,
// only forward, the share of the whole start, the end once the spawn area is meshed and the session
// plays, the longest wait, the test hook's notation, and the spawn area as the client world and the
// mesh scheduler hold it.
class TestLoadingProgress : public QObject
{
    Q_OBJECT

private slots:
    // A local start runs content, world, joining, chunks, meshes and ends ready, with the share of
    // the start growing at every step; the ready signal comes once.
    void localStartRunsEveryStage()
    {
        QcLoadingProgress progress;
        QSignalSpy ready(&progress, &QcLoadingProgress::ready);
        QCOMPARE(progress.stage(), Stage::Idle);
        QVERIFY(!progress.active());

        progress.begin(Kind::NewWorld);
        QCOMPARE(progress.stage(), Stage::Content);
        QCOMPARE(progress.kind(), Kind::NewWorld);
        QVERIFY(progress.active());
        double last = progress.fraction();
        QCOMPARE(last, 0.0);

        progress.advance(Stage::World);
        QCOMPARE(progress.stage(), Stage::World);
        QVERIFY(progress.fraction() > last);
        last = progress.fraction();
        progress.advance(Stage::Joining);
        QCOMPARE(progress.stage(), Stage::Joining);
        QVERIFY(progress.fraction() > last);
        last = progress.fraction();

        progress.observe({.needed = 49, .arrived = 10, .meshed = 0}, true);
        QCOMPARE(progress.stage(), Stage::Chunks);
        QCOMPARE(progress.chunksArrived(), 10);
        QCOMPARE(progress.chunksNeeded(), 49);
        QVERIFY(progress.fraction() > last);
        last = progress.fraction();

        progress.observe({.needed = 49, .arrived = 49, .meshed = 20}, true);
        QCOMPARE(progress.stage(), Stage::Meshes);
        QCOMPARE(progress.chunksMeshed(), 20);
        QVERIFY(progress.fraction() > last);
        QVERIFY(progress.fraction() < 1.0);
        QCOMPARE(ready.count(), 0);

        progress.observe({.needed = 49, .arrived = 49, .meshed = 49}, true);
        QCOMPARE(progress.stage(), Stage::Ready);
        QCOMPARE(progress.fraction(), 1.0);
        QVERIFY(!progress.active());
        QCOMPARE(ready.count(), 1);
        // Nothing moves once ready.
        progress.observe({.needed = 49, .arrived = 49, .meshed = 49}, true);
        QCOMPARE(ready.count(), 1);
    }

    // Steps out of order are refused with a warning and change nothing.
    void stepsOutOfOrderAreRefused()
    {
        QcLoadingProgress progress;
        progress.advance(Stage::World);
        QCOMPARE(progress.stage(), Stage::Idle);

        progress.begin(Kind::SavedWorld);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"refused the step to"_s));
        progress.advance(Stage::Joining);
        QCOMPARE(progress.stage(), Stage::Content);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"refused the step to"_s));
        progress.advance(Stage::Ready);
        QCOMPARE(progress.stage(), Stage::Content);
    }

    // Before the session plays the player's place is unknown: the area changes nothing. Once it plays
    // the stage never goes back, even when chunks leave the area.
    void stageOnlyGoesForward()
    {
        QcLoadingProgress progress;
        progress.begin(Kind::Network);
        QCOMPARE(progress.stage(), Stage::Joining);

        progress.observe({.needed = 9, .arrived = 9, .meshed = 9}, false);
        QCOMPARE(progress.stage(), Stage::Joining);
        QCOMPARE(progress.chunksNeeded(), 0);

        progress.observe({.needed = 9, .arrived = 9, .meshed = 3}, true);
        QCOMPARE(progress.stage(), Stage::Meshes);
        progress.observe({.needed = 9, .arrived = 4, .meshed = 0}, true);
        QCOMPARE(progress.stage(), Stage::Meshes);
        QCOMPARE(progress.chunksArrived(), 4);
    }

    // A spawn area that does not get meshed in time lets the world show anyway, with a warning.
    void longestWaitEndsTheLoading()
    {
        QcLoadingProgress progress;
        QSignalSpy ready(&progress, &QcLoadingProgress::ready);
        progress.setMaxWaitMs(0);
        progress.begin(Kind::NewWorld);
        progress.advance(Stage::World);
        progress.advance(Stage::Joining);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"is not meshed after 0 ms"_s));
        progress.observe({.needed = 49, .arrived = 3, .meshed = 0}, true);
        QCOMPARE(progress.stage(), Stage::Ready);
        QCOMPARE(ready.count(), 1);
    }

    // A start that ends without a world leaves the progress idle; a new start begins afresh.
    void resetAndBeginAgain()
    {
        QcLoadingProgress progress;
        progress.begin(Kind::NewWorld);
        progress.advance(Stage::World);
        progress.reset();
        QCOMPARE(progress.stage(), Stage::Idle);
        QVERIFY(!progress.active());

        progress.begin(Kind::SavedWorld);
        QCOMPARE(progress.stage(), Stage::Content);
        QCOMPARE(progress.kind(), Kind::SavedWorld);
        QCOMPARE(progress.chunksNeeded(), 0);
    }

    // Entering another dimension starts at joining, names where it goes, cannot be cancelled
    // (cancelling leaves the game) and ends at its longest wait like any start (decision E-1002).
    void aDimensionStartCannotBeCancelled()
    {
        QcLoadingProgress progress;
        progress.begin(Kind::Network);
        QVERIFY(progress.cancellable());
        QVERIFY(progress.destination().isEmpty());

        QSignalSpy ready(&progress, &QcLoadingProgress::ready);
        progress.setMaxWaitMs(0);
        progress.begin(Kind::Dimension, u"the Nether"_s);
        QCOMPARE(progress.stage(), Stage::Joining);
        QCOMPARE(progress.destination(), u"the Nether"_s);
        QVERIFY(!progress.cancellable());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"is not meshed after 0 ms"_s));
        progress.observe({.needed = 49, .arrived = 3, .meshed = 0}, true);
        QCOMPARE(progress.stage(), Stage::Ready);
        QCOMPARE(ready.count(), 1);

        progress.begin(Kind::SavedWorld);
        QVERIFY(!progress.cancellable());
    }

    // The spawn area fits inside what the server streams: its outer ring is never meshed.
    void spawnRadiusFitsTheStreamedChunks()
    {
        QCOMPARE(QcLoadingProgress::spawnRadiusFor(12), QcLoadingProgress::spawnRadius);
        QCOMPARE(QcLoadingProgress::spawnRadiusFor(4), 3);
        QCOMPARE(QcLoadingProgress::spawnRadiusFor(3), 2);
        QCOMPARE(QcLoadingProgress::spawnRadiusFor(1), 0);
        QCOMPARE(QcLoadingProgress::spawnRadiusFor(0), 0);
    }

    // "loading:<stage>[,<kind>[,<arrived>,<meshed>,<needed>]]" of the test hook.
    void showNotation()
    {
        const std::optional<QcLoadingProgress::Shown> chunks = QcLoadingProgress::parseShow(u"chunks,savedworld,12,0,49");
        QVERIFY(chunks);
        QCOMPARE(chunks->stage, Stage::Chunks);
        QCOMPARE(chunks->kind, Kind::SavedWorld);
        QCOMPARE(chunks->area.arrived, 12);
        QCOMPARE(chunks->area.meshed, 0);
        QCOMPARE(chunks->area.needed, 49);

        const std::optional<QcLoadingProgress::Shown> content = QcLoadingProgress::parseShow(u"Content");
        QVERIFY(content);
        QCOMPARE(content->stage, Stage::Content);
        QCOMPARE(content->kind, Kind::NewWorld);

        QVERIFY(!QcLoadingProgress::parseShow(u"bogus"));
        QVERIFY(!QcLoadingProgress::parseShow(u"chunks,elsewhere"));
        QVERIFY(!QcLoadingProgress::parseShow(u"chunks,newworld,1,2"));
        QVERIFY(!QcLoadingProgress::parseShow(u"chunks,newworld,1,x,3"));
        QVERIFY(!QcLoadingProgress::parseShow(u"chunks,newworld,-1,0,3"));
        // No loading screen to show, and counts that cannot be.
        QVERIFY(!QcLoadingProgress::parseShow(u"idle"));
        QVERIFY(!QcLoadingProgress::parseShow(u"ready,network"));
        QVERIFY(!QcLoadingProgress::parseShow(u"meshes,newworld,10,11,49"));
        QVERIFY(!QcLoadingProgress::parseShow(u"chunks,newworld,50,0,49"));
        QVERIFY(QcLoadingProgress::parseShow(u"meshes,newworld,49,49,49"));
    }

    // A state the test hook shows is no start: it is not active, a playing session with a meshed
    // area leaves it (no ready()), and begin() starts a real one from there.
    void shownStateIsNoStart()
    {
        QcLoadingProgress progress;
        QSignalSpy ready(&progress, &QcLoadingProgress::ready);
        progress.show(*QcLoadingProgress::parseShow(u"chunks,newworld,12,0,49"));
        QCOMPARE(progress.stage(), Stage::Chunks);
        QCOMPARE(progress.chunksArrived(), 12);
        QVERIFY(progress.fraction() > 0.2 && progress.fraction() < 0.7);
        QVERIFY(!progress.active());

        progress.observe({.needed = 9, .arrived = 9, .meshed = 9}, true);
        QCOMPARE(progress.stage(), Stage::Chunks);
        QCOMPARE(progress.chunksArrived(), 12);
        QCOMPARE(ready.count(), 0);

        progress.begin(Kind::SavedWorld);
        QVERIFY(progress.active());
        progress.advance(Stage::World);
        progress.advance(Stage::Joining);
        progress.observe({.needed = 9, .arrived = 9, .meshed = 9}, true);
        QCOMPARE(progress.stage(), Stage::Ready);
        QCOMPARE(ready.count(), 1);

        // reset() ends a shown state too.
        progress.show(*QcLoadingProgress::parseShow(u"meshes"));
        progress.reset();
        QCOMPARE(progress.stage(), Stage::Idle);
        QVERIFY(!progress.active());
    }

    // The spawn area counts the chunks that arrived and those meshed with their side neighbours
    // settled: with a 3 x 3 square only the centre has all its neighbours, with 5 x 5 every chunk
    // of the 3 x 3 area around the centre is meshed once the scheduler is idle.
    void spawnAreaFollowsWorldAndMeshes()
    {
        const std::shared_ptr<QcContentRegistry> content = stoneContent();
        QcClientWorld world(content);
        QcMeshScheduler meshes;
        meshes.setRenderTable(std::make_shared<const QcBlockRenderTable>(*content, QcTextureAtlas::build(*content)));
        QCOMPARE(QcLoadingProgress::measure(world, meshes, {0, 0}, 1).needed, 9);
        QCOMPARE(QcLoadingProgress::measure(world, meshes, {0, 0}, 1).arrived, 0);

        load(world, meshes, 1);
        Area area = QcLoadingProgress::measure(world, meshes, {0, 0}, 1);
        QCOMPARE(area.arrived, 9);
        QCOMPARE(area.meshed, 0);
        pumpUntilIdle(meshes, world);
        QCOMPARE(QcLoadingProgress::measure(world, meshes, {0, 0}, 1).meshed, 1);

        load(world, meshes, 2);
        pumpUntilIdle(meshes, world);
        area = QcLoadingProgress::measure(world, meshes, {0, 0}, 1);
        QCOMPARE(area.arrived, 9);
        QCOMPARE(area.meshed, 9);
    }

private:
    // Loads the square of chunks from -radius to radius that is not loaded yet.
    static void load(QcClientWorld &world, QcMeshScheduler &meshes, int radius)
    {
        for (int x = -radius; x <= radius; ++x) {
            for (int z = -radius; z <= radius; ++z) {
                if (!world.chunk({x, z})) {
                    world.setChunk(stoneChunk({x, z}));
                    meshes.chunkLoaded(world, {x, z});
                }
            }
        }
    }

    static void pumpUntilIdle(QcMeshScheduler &meshes, const QcClientWorld &world)
    {
        QVERIFY(QTest::qWaitFor(
            [&] {
                meshes.pump(world);
                return meshes.dirtyCount() == 0 && meshes.jobsInFlight() == 0 && meshes.lightJobsPending() == 0;
            },
            timeoutMs));
    }
};

QTEST_GUILESS_MAIN(TestLoadingProgress)
#include "test_loading_progress.moc"
