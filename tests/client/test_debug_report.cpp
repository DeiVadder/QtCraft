#include "clientcore/debug/QcDebugJson.h"
#include "clientcore/debug/QcDebugLines.h"
#include "clientcore/debug/QcDebugText.h"
#include "clientcore/debug/QcStatsLog.h"
#include "clientcore/light/QcLightProbe.h"
#include "clientcore/session/QcClientDebugSubscription.h"
#include "clientcore/session/QcClientWorld.h"
#include "clientcore/session/QcServerDebugMirror.h"
#include "common/core/QcSectionKey.h"
#include "common/core/QcWorldConstants.h"
#include "QcTestUtil.h"
#include "QcTestWorld.h"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// Test content plus a lamp (light 12) that lets no light through.
std::shared_ptr<const QcContentRegistry> lampContent()
{
    auto content = std::make_shared<QcContentRegistry>(qcMakeTestContent());
    QcBlockDefinition lamp;
    lamp.name = u"test:lamp"_s;
    lamp.lightLevel = 12;
    lamp.faceTextures.fill(u"test:stone"_s);
    if (!content->registerBlock(lamp)) {
        QcTestUtil::failCase(QStringLiteral("cannot register test:lamp"));
    }
    return content;
}

// A world of the 5 x 5 chunks around the origin, empty (air) up to the sky.
std::unique_ptr<QcClientWorld> emptyWorld(const std::shared_ptr<const QcContentRegistry> &content)
{
    auto world = std::make_unique<QcClientWorld>(content);
    for (int x = -2; x <= 2; ++x) {
        for (int z = -2; z <= 2; ++z) {
            world->setChunk(std::make_shared<QcChunk>(QcChunkPos{x, z}));
        }
    }
    return world;
}

void fill(QcClientWorld &world, QcBlockPos from, QcBlockPos to, QcBlockId block)
{
    for (int x = from.x; x <= to.x; ++x) {
        for (int y = from.y; y <= to.y; ++y) {
            for (int z = from.z; z <= to.z; ++z) {
                world.setBlock({x, y, z}, block);
            }
        }
    }
}

QcDebugSnapshot fullSnapshot()
{
    QcDebugSnapshot snapshot;
    snapshot.protocol = 20;
    snapshot.seed = 42;
    snapshot.position = {-17.25, 64.0, 3.5};
    snapshot.yaw = 92.0f;
    snapshot.pitch = -12.5f;
    snapshot.feetLight = {15, 3};
    snapshot.target = QcDebugSnapshot::Target{{-18, 63, 3}, QcBlockFace::Up, u"qtcraft:oak_stairs"_s,
                                              {{u"facing"_s, u"east"_s}, {u"half"_s, u"top"_s}}, {u"qtcraft:wooden_stairs"_s}, {14, 0}};
    snapshot.entities = {3, 1, 2, 0};
    snapshot.loadedChunks = 81;
    snapshot.memoryMb = 312.4;
    snapshot.peakMemoryMb = 330.0;

    QcDebugSnapshot::Frame frame;
    frame.fps = 119.6;
    frame.phases[static_cast<std::size_t>(QcFramePhase::Interval)] = {8.34, 12.5};
    frame.phases[static_cast<std::size_t>(QcFramePhase::GuiCpu)] = {0.41, 1.2};
    frame.phases[static_cast<std::size_t>(QcFramePhase::Gpu)] = {0.9, 1.1};
    snapshot.frame = frame;

    QcMsg::ServerDebug server;
    server.phaseMeanMs[static_cast<std::size_t>(QcTickPhase::Mobs)] = 0.25f;
    server.phaseMaxMs[static_cast<std::size_t>(QcTickPhase::Mobs)] = 1.5f;
    server.tickMeanMs = 0.5f;
    server.tickMaxMs = 2.0f;
    server.biome = u"qtcraft:plains"_s;
    server.worldType = u"showcase"_s;
    server.goals = {{7, QcMsg::MobGoal::Kind::Chase, {1.0, 2.0, 3.0}}};
    snapshot.server = server;
    return snapshot;
}

} // namespace

class TestDebugReport : public QObject
{
    Q_OBJECT

private slots:
    void debugSubscriptionGoesOutOnlyWhileWelcomed()
    {
        QcClientDebugSubscription subscription;
        QList<quint8> sent;
        subscription.setSender([&sent](const QcMessage &message) { sent.append(std::get<QcMsg::DebugSubscribe>(message).flags); });

        // Before the welcome nothing goes out; the welcome sends what is wanted.
        subscription.setFlags(QcMsg::DebugSubscribe::all);
        QVERIFY(sent.isEmpty());
        subscription.welcomed();
        QCOMPARE(sent, QList<quint8>{QcMsg::DebugSubscribe::all});
        subscription.setFlags(0);
        QCOMPARE(sent, (QList<quint8>{QcMsg::DebugSubscribe::all, 0}));

        // After the connection closed, changes stay with the client.
        subscription.closed();
        subscription.setFlags(QcMsg::DebugSubscribe::stats);
        subscription.setBaseFlags(QcMsg::DebugSubscribe::all);
        QCOMPARE(sent.size(), 2);
        QCOMPARE(subscription.flags(), QcMsg::DebugSubscribe::all);
    }

    void skyLightReachesUnderARoofFromItsEdge()
    {
        const auto content = lampContent();
        auto world = emptyWorld(content);
        const QcBlockId stone = *content->blockId(u"test:stone"_s);

        // Open sky everywhere above the ground.
        fill(*world, {-30, 60, -30}, {30, 60, 30}, stone);
        QCOMPARE(QcLightProbe(*world, {0, 61, 0}).at({0, 61, 0}).sky, 15);
        QCOMPARE(QcLightProbe(*world, {0, 61, 0}).at({0, 61, 0}).block, 0);

        // A roof from x = -30 to 4: light enters at its edge (x = 5) and loses one level per block.
        fill(*world, {-30, 64, -30}, {4, 64, 30}, stone);
        QCOMPARE(QcLightProbe(*world, {4, 62, 0}).at({4, 62, 0}).sky, 14);
        QCOMPARE(QcLightProbe(*world, {0, 62, 0}).at({0, 62, 0}).sky, 10);
        QCOMPARE(QcLightProbe(*world, {-12, 62, 0}).at({-12, 62, 0}).sky, 0);
        // Inside the stone nothing shines; the cell above the roof sees the sky.
        QCOMPARE(QcLightProbe(*world, {0, 64, 0}).at({0, 64, 0}).sky, 0);
        QCOMPARE(QcLightProbe(*world, {0, 65, 0}).at({0, 65, 0}).sky, 15);
    }

    void blockLightAndTheCellsAroundTheCentre()
    {
        const auto content = lampContent();
        auto world = emptyWorld(content);
        world->setBlock({2, 70, 2}, *content->blockId(u"test:lamp"_s));

        const QcLightProbe probe(*world, {2, 71, 2}, 2);
        QCOMPARE(probe.at({2, 71, 2}).block, 11);
        QCOMPARE(probe.at({2, 72, 2}).block, 10);
        QCOMPARE(probe.at({4, 71, 3}).block, 8);
        QCOMPARE(probe.at({2, 70, 2}).block, 12);
        // Outside the probed cube both are unknown.
        QCOMPARE(probe.at({5, 71, 2}).block, -1);
        QCOMPARE(probe.at({5, 71, 2}).sky, -1);
        QCOMPARE(QcLightProbe::maxRadius, 3);
        // A single cell under the open sky takes the cheap path for the sky, the flood for the lamp.
        const QcLightProbe single(*world, {2, 71, 2});
        QCOMPARE(single.at({2, 71, 2}).sky, 15);
        QCOMPARE(single.at({2, 71, 2}).block, 11);
    }

    void unloadedChunksAreDark()
    {
        const auto content = lampContent();
        QcClientWorld world(content);
        QCOMPARE(QcLightProbe(world, {0, 64, 0}).at({0, 64, 0}).sky, 0);
        // Above the world the sky is open, below it nothing is.
        QCOMPARE(QcLightProbe(world, {0, -1, 0}).at({0, -1, 0}).sky, -1);
    }

    void facingFollowsTheYaw()
    {
        QCOMPARE(QByteArrayView(QcDebugSnapshot::facing(0.0f)), "north");
        QCOMPARE(QByteArrayView(QcDebugSnapshot::facing(44.0f)), "north");
        QCOMPARE(QByteArrayView(QcDebugSnapshot::facing(46.0f)), "east");
        QCOMPARE(QByteArrayView(QcDebugSnapshot::facing(180.0f)), "south");
        QCOMPARE(QByteArrayView(QcDebugSnapshot::facing(-90.0f)), "west");
        QCOMPARE(QByteArrayView(QcDebugSnapshot::facing(719.0f)), "north");
    }

    void compactLineShowsRateAndFrameTime()
    {
        QCOMPARE(QcDebugText::compact(fullSnapshot()), u"120 FPS · 8.34 ms (max 12.5 ms)"_s);
        QCOMPARE(QcDebugText::compact(QcDebugSnapshot{}), u"No frame statistics"_s);

        // A frame rate limit in force is named (decision E-835).
        QcDebugSnapshot limited = fullSnapshot();
        limited.frame->frameLimit = 60;
        QCOMPARE(QcDebugText::compact(limited), u"120 FPS · 8.34 ms (max 12.5 ms) · limit 60 FPS"_s);
    }

    void fullColumnNamesPlaceTargetAndServer()
    {
        const QStringList lines = QcDebugText::general(fullSnapshot());
        QCOMPARE(lines.first(), u"120 FPS · 8.34 ms (max 12.5 ms)"_s);
        QVERIFY(lines.contains(u"XYZ: -17.250 / 64.000 / 3.500"_s));
        // Block -18 64 3 lies in chunk -2 0 at 14 3, section 4.
        QVERIFY(lines.contains(u"Block: -18 64 3 · chunk -2 0 (at 14 3) · section 4"_s));
        QVERIFY(lines.contains(u"Facing: east (yaw 92.0°, pitch -12.5°)"_s));
        QVERIFY(lines.contains(u"Light at the feet: sky 15, block 3"_s));
        QVERIFY(lines.contains(u"Biome: qtcraft:plains"_s));
        QVERIFY(lines.contains(u"Target: qtcraft:oak_stairs at -18 63 3"_s));
        QVERIFY(lines.contains(u"  State: facing=east, half=top"_s));
        QVERIFY(lines.contains(u"  Tags: qtcraft:wooden_stairs"_s));
        QVERIFY(lines.contains(u"  Light in front: sky 14, block 0"_s));
        QVERIFY(lines.contains(u"Entities: 3 creatures, 1 players, 2 items, 0 falling blocks"_s));
        QVERIFY(lines.contains(u"Memory: 312 MiB (peak 330 MiB)"_s));
        QVERIFY(std::ranges::any_of(lines, [](const QString &line) { return line.startsWith(u"Server tick 0: 0.50 ms (max 2.00 ms)"_s); }));
        QVERIFY(lines.contains(u"Protocol 20 · world type showcase · seed 42"_s));
    }

    void timeColumnIsAligned()
    {
        const QStringList lines = QcDebugText::timings(fullSnapshot());
        // Header and phases of the frame, header, phases and the whole tick of the server.
        QCOMPARE(lines.size(), 1 + QcFramePhases::count + 1 + QcTickPhases::count + 1);
        for (const QString &line : lines) {
            QCOMPARE(line.size(), lines.first().size());
        }
        QVERIFY(lines.contains(QcDebugText::phaseName(QcTickPhase::Mobs).leftJustified(lines.first().size() - 16) + u"    0.25    1.50"_s));
        // Without RHI timestamps the GPU row shows no numbers.
        QcDebugSnapshot untimed = fullSnapshot();
        QVERIFY(QcDebugText::timings(untimed).filter(QcDebugText::phaseName(QcFramePhase::Gpu)).first().endsWith(u"–"_s));
        untimed.frame->gpuTimed = true;
        QVERIFY(QcDebugText::timings(untimed).filter(QcDebugText::phaseName(QcFramePhase::Gpu)).first().endsWith(u"1.10"_s));

        // Headless: no frame part, no server part before the server answered.
        QVERIFY(QcDebugText::timings(QcDebugSnapshot{}).isEmpty());
    }

    void jsonCarriesTheSameNumbers()
    {
        QcDebugSnapshot snapshot = fullSnapshot();
        snapshot.frame->gpuTimed = true;
        const QJsonObject json = QcDebugJson::toJson(snapshot);

        const QJsonObject client = json.value(u"client"_s).toObject();
        QCOMPARE(client.value(u"block"_s).toArray(), (QJsonArray{-18, 64, 3}));
        QCOMPARE(client.value(u"chunk"_s).toArray(), (QJsonArray{-2, 0}));
        QCOMPARE(client.value(u"section"_s).toInt(), 4);
        QCOMPARE(client.value(u"facing"_s).toString(), u"east"_s);
        QCOMPARE(client.value(u"light"_s).toObject().value(u"block"_s).toInt(), 3);
        const QJsonObject target = client.value(u"target"_s).toObject();
        QCOMPARE(target.value(u"state"_s).toObject().value(u"half"_s).toString(), u"top"_s);
        QCOMPARE(target.value(u"light"_s).toObject().value(u"sky"_s).toInt(), 14);
        QCOMPARE(client.value(u"entities"_s).toObject().value(u"items"_s).toInt(), 2);

        const QJsonObject frame = json.value(u"frame"_s).toObject();
        QCOMPARE(frame.value(u"fps"_s).toDouble(), 119.6);
        QCOMPARE(frame.value(u"phases"_s).toObject().value(u"guiCpu"_s).toObject().value(u"max"_s).toDouble(), 1.2);
        QCOMPARE(frame.value(u"phases"_s).toObject().size(), QcFramePhases::count);
        snapshot.frame->gpuTimed = false;
        QVERIFY(!QcDebugJson::toJson(snapshot).value(u"frame"_s).toObject().value(u"phases"_s).toObject().contains(u"gpu"_s));

        const QJsonObject server = json.value(u"server"_s).toObject();
        QCOMPARE(server.value(u"phases"_s).toObject().size(), QcTickPhases::count);
        QCOMPARE(server.value(u"phases"_s).toObject().value(u"mobs"_s).toObject().value(u"mean"_s).toDouble(), 0.25);
        QCOMPARE(server.value(u"tickMs"_s).toObject().value(u"max"_s).toDouble(), 2.0);
        QCOMPARE(server.value(u"goals"_s).toArray().first().toObject().value(u"kind"_s).toString(), u"chase"_s);

        // Without a frame or server part they are left out.
        const QJsonObject bare = QcDebugJson::toJson(QcDebugSnapshot{});
        QVERIFY(bare.contains(u"client"_s));
        QVERIFY(!bare.contains(u"frame"_s));
        QVERIFY(!bare.contains(u"server"_s));
    }

    void linesForBoxesSectionsAndFlashes()
    {
        // Vertices lie relative to the origin of the lines (decision E-883), exact far out.
        const QcVec3 far{1e8, 0.0, -1e8};
        QcDebugLines lines(far);
        lines.addBox({far + QcVec3{0.0, 0.0, 0.0}, far + QcVec3{1.0, 2.0, 1.0}}, QcDebugLines::hitboxColor);
        // Twelve edges, two ends each, all within the box.
        QCOMPARE(lines.vertices().size(), 24);
        for (const QcDebugLineVertex &vertex : lines.vertices()) {
            QVERIFY(vertex.x >= 0.0f && vertex.x <= 1.0f && vertex.y >= 0.0f && vertex.y <= 2.0f);
            QCOMPARE(vertex.rgba[3], quint8(255));
        }

        // Section keys of negative chunks come back as the right box.
        const QcAabb box = QcDebugLines::sectionBox(qcSectionKey({-3, 5}, 4));
        QVERIFY(std::abs(box.min.x - -48.0) < 0.05 && std::abs(box.max.x - -32.0) < 0.05);
        QVERIFY(std::abs(box.min.y - 64.0) < 0.05 && std::abs(box.max.y - 80.0) < 0.05);
        QVERIFY(std::abs(box.min.z - 80.0) < 0.05 && std::abs(box.max.z - 96.0) < 0.05);

        // A fresh mesh flashes brightly, an older one fainter, one past the flash not at all.
        QcDebugLines flashes(QcVec3{});
        const QHash<quint64, qint64> meshedAt{{qcSectionKey({0, 0}, 1), 1000}, {qcSectionKey({1, 0}, 1), 400}};
        flashes.addRemeshFlashes(meshedAt, 1000 + QcDebugLines::flashMs / 2);
        QCOMPARE(flashes.vertices().size(), 24);
        QVERIFY(std::abs(int(flashes.vertices().first().rgba[3]) - 127) <= 1);

        // Chunk borders: corners of the 6 x 6 chunk corners around, rings at the 17 section
        // borders, the wall grid; nothing outside the reach of the corners.
        QcDebugLines borders(QcVec3{});
        borders.addChunkBorders({8.5, 70.0, -3.5});
        constexpr qsizetype corners = 36;
        constexpr qsizetype rings = 204;
        constexpr qsizetype grid = 12 + 60;
        QCOMPARE(borders.vertices().size(), 2 * (corners + rings + grid));
        for (const QcDebugLineVertex &vertex : borders.vertices()) {
            QVERIFY(vertex.x >= -32.0f && vertex.x <= 48.0f && vertex.z >= -48.0f && vertex.z <= 32.0f);
        }
    }

    void sectionKeysDecode()
    {
        for (const QcChunkPos chunk : {QcChunkPos{0, 0}, QcChunkPos{-1, 1}, QcChunkPos{-QcWorld::maxHorizontalChunk, QcWorld::maxHorizontalChunk}}) {
            for (const int section : {0, 7, QcWorld::sectionCount - 1}) {
                const QcSectionAddress address = qcSectionFromKey(qcSectionKey(chunk, section));
                QCOMPARE(address.chunk, chunk);
                QCOMPARE(address.sectionIndex, section);
            }
        }
    }

    void tickGraphHasEachTickOnce()
    {
        QcServerDebugMirror mirror;
        QcMsg::ServerDebug first;
        first.tick = 10;
        first.recentTickMs = {1.0f, 2.0f, 3.0f};
        mirror.receive(first);

        // A message that reports ticks 9 to 12 adds only 11 and 12.
        QcMsg::ServerDebug overlapping;
        overlapping.tick = 12;
        overlapping.recentTickMs = {2.0f, 3.0f, 4.0f, 5.0f};
        mirror.receive(overlapping);
        QCOMPARE(mirror.tickHistory(), (QList<float>{1.0f, 2.0f, 3.0f, 4.0f, 5.0f}));

        // Nothing newer: nothing added, but it is the latest message.
        QcMsg::ServerDebug repeated = overlapping;
        repeated.recentTickMs = {9.0f};
        mirror.receive(repeated);
        QCOMPARE(mirror.tickHistory().size(), 5);
        QCOMPARE(mirror.latest(), std::optional(repeated));

        // After a gap all reported ticks are new; after clear() everything starts over.
        QcMsg::ServerDebug later;
        later.tick = 40;
        later.recentTickMs = {6.0f, 7.0f};
        mirror.receive(later);
        QCOMPARE(mirror.tickHistory().size(), 7);
        mirror.clear();
        mirror.receive(overlapping);
        QCOMPARE(mirror.tickHistory(), overlapping.recentTickMs);
    }

    void statsLogIntervalAndLines()
    {
        qunsetenv("QTCRAFT_STATS");
        QCOMPARE(QcStatsLog::interval(u"250"_s), 250);
        QCOMPARE(QcStatsLog::interval(u"20"_s), QcStatsLog::minIntervalMs);
        QCOMPARE(QcStatsLog::interval(u"soon"_s), 0);
        QCOMPARE(QcStatsLog::interval(u"-5"_s), 0);
        QCOMPARE(QcStatsLog::interval({}), 0);
        qputenv("QTCRAFT_STATS", "1000");
        QCOMPARE(QcStatsLog::interval({}), 1000);
        // The option wins over the variable.
        QCOMPARE(QcStatsLog::interval(u"300"_s), 300);
        qunsetenv("QTCRAFT_STATS");

        // Nothing is logged while there is no data, then one compact JSON line per interval.
        bool ready = false;
        QcStatsLog log(100, [&ready]() -> std::optional<QJsonObject> {
            return ready ? std::optional(QJsonObject{{u"a"_s, 1}}) : std::nullopt;
        });
        QSignalSpy lines(&log, &QcStatsLog::logged);
        QTest::qWait(250);
        QCOMPARE(lines.size(), 0);
        ready = true;
        QVERIFY(lines.wait(1000));
        QCOMPARE(lines.first().first().toString(), uR"({"a":1})"_s);
    }
};

QTEST_GUILESS_MAIN(TestDebugReport)
#include "test_debug_report.moc"
