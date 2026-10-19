#include "QcMachineFixture.h"
#include "server/network/QcServerSettings.h"

#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// The simulation limits of a server (decision E-349): defaults, clamping with warnings, server.json
// on a dedicated server, world.json in a single-player world, and /limits.
class TestSimulationLimits : public QcMachineFixture
{
    Q_OBJECT

private:
    // Fifty hoppers over chests, each with an item, all ready at once.
    void readyHoppers()
    {
        for (int i = 0; i < 50; ++i) {
            const int x = i % 10;
            const int z = (i / 10) * 2;
            put(x, 0, z, u"chest[facing=south]"_s);
            put(x, 1, z, u"signal_hopper[facing=down]"_s);
            put(x, 2, z, u"stone"_s);
            fill(x, 1, z, {stack("qtcraft:diamond", 1)});
        }
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void defaultsAreUnchanged()
    {
        const QcServerLimits limits;
        QCOMPARE(limits.signalBudget, 8192);
        QCOMPARE(limits.signalScheduleReservePercent, 25);
        QCOMPARE(limits.scheduleReserve(), 2048);
        QCOMPARE(limits.signalWorkPerChange, 4);
        QCOMPARE(limits.signalMinimumStep, 64);
        QCOMPARE(limits.signalMaxPendingChanges, qint64(1) << 20);
        QCOMPARE(limits.hopperBudget, 4096);
        QCOMPARE(limits.hopperCooldownTicks, 8);
        // The bounds of older stages kept their values when they moved here (stage 31.1).
        QCOMPARE(limits.maxContainers, 16384);
        QCOMPARE(limits.maxMachines, 4096);
        QCOMPARE(limits.maxSaplings, 4096);
        QCOMPARE(limits.blockUpdatesPerTick, 1024);
        QCOMPARE(limits.waitingBlockUpdates, 65536);
        QCOMPARE(limits.commandBlockUpdates, qint64(1) << 20);
        QCOMPARE(limits.savedBlockUpdates, qint64(4) << 20);
        QCOMPARE(limits.liquidStepsPerTick, 512);
        QCOMPARE(limits.liquidPlannedSteps, 262144);
        // Fire (stage 34, decision E-392).
        QCOMPARE(limits.fireStepsPerTick, 256);
        QCOMPARE(limits.firesPerChunk, 256);
        QCOMPARE(limits.fires, 4096);
        QCOMPARE(limits.fireSpreadRadius, 12);
        QCOMPARE(limits.fallingBlocks, 2048);
        QCOMPARE(limits.itemsAwake, 512);
        QCOMPARE(limits.itemsQueued, 1024);
        QCOMPARE(limits.itemsTotal, 4096);
        QCOMPARE(limits.hostileCreaturesPerPlayer, 8);
        QCOMPARE(limits.passiveCreaturesPerPlayer, 6);
        QCOMPARE(limits.ambientCreaturesPerPlayer, 10);
        QCOMPARE(limits.breedingCrowd, 24);
        QCOMPARE(limits.bredPerKind, 256);
        QCOMPARE(limits.randomTicksPerSection, 3);
        QCOMPARE(limits.leafDecaysPerTick, 8);
        QCOMPARE(limits.leafChecksPerTick, 64);
        QCOMPARE(limits.leafPendingChecks, 16384);
        QCOMPARE(limits.maxArmourStands, 4096);
        QCOMPARE(limits.maxMinecarts, 4096);
        QCOMPARE(limits.movingBlocksShown, 4096);
        QCOMPARE(limits.experienceOrbs, 1024);
        QCOMPARE(limits.projectiles, 1024);
        QCOMPARE(limits.projectilesPerChunk, 64);
        // Path finding of creatures (stage 37.1, decision E-400).
        QCOMPARE(limits.pathNodesPerTick, 4096);
        QCOMPARE(limits.pathNodesPerSearch, 512);
        QCOMPARE(limits.pathLength, 64);
        // Crowds of creatures (stage 37.1, decision E-401).
        QCOMPARE(limits.crowdThrottled, 8);
        QCOMPARE(limits.crowdIdle, 24);
        // Villagers (stage 37.2b, decision E-411).
        QCOMPARE(limits.villagersPerVillage, 32);
        QCOMPARE(limits.villagers, 1024);
        QCOMPARE(limits.villagerThoughtsPerTick, 64);
        QCOMPARE(limits.villagerRoutesPerTick, 8);
        QCOMPARE(limits.villagerHarvestsPerTick, 4);
        QCOMPARE(limits.villageGolems, 256);
        QCOMPARE(limits.waterCreaturesPerPlayer, 5);
        // Every field is in the table once, with its default inside its range.
        QSet<QString> keys;
        for (const QcServerLimits::Field &field : QcServerLimits::fields()) {
            QVERIFY(!keys.contains(QLatin1StringView(field.key)));
            keys.insert(QLatin1StringView(field.key));
            QVERIFY(limits.*(field.value) >= field.low && limits.*(field.value) <= field.high);
        }
        QCOMPARE(limits.spawnerCreatures, 256);
        QCOMPARE(limits.guardians, 64);
        QCOMPARE(keys.size(), qsizetype(70));
        const QcServerLimits::Read defaults = QcServerLimits::fromJson({}, u"server.json"_s);
        QCOMPARE(defaults.limits, limits);
        QVERIFY(defaults.warnings.isEmpty());
        // What a server writes reads back the same.
        const QcServerLimits::Read written = QcServerLimits::fromJson(limits.toJson(), u"server.json"_s);
        QCOMPARE(written.limits, limits);
        QVERIFY(written.warnings.isEmpty());
    }

    void outOfRangeIsClamped()
    {
        const QJsonObject json{{u"hopperBudget"_s, 0}, {u"signalBudget"_s, 1e12}, {u"hopperCooldownTicks"_s, 2.5},
                               {u"signalMinimumStep"_s, u"many"_s}, {u"noSuchLimit"_s, 3}};
        const QcServerLimits::Read read = QcServerLimits::fromJson(json, u"server.json"_s);
        const QcServerLimits &limits = read.limits;
        const QStringList &warnings = read.warnings;
        QCOMPARE(limits.hopperBudget, 1);
        QCOMPARE(limits.signalBudget, qint64(1) << 22);
        QCOMPARE(limits.hopperCooldownTicks, 3);
        QCOMPARE(limits.signalMinimumStep, 64);
        QCOMPARE(warnings.size(), 5);
        QVERIFY(warnings.join(u'\n').contains(u"noSuchLimit"_s));
    }

    void serverJsonLimitTakesEffect()
    {
        const QcResult<QcServerSettings> settings = QcServerSettings::fromJson(
            QJsonDocument::fromJson(R"({"limits": {"hopperBudget": 10, "hopperCooldownTicks": 20}})").object());
        QVERIFY(settings.has_value());
        QcServerConfig config = settings->serverConfig();
        QVERIFY(config.limitsFromServer);
        buildArea(config);
        readyHoppers();
        ticks(1);
        QCOMPARE(hopperStats().acting, 10);
        ticks(4);
        QCOMPARE(everywhere("qtcraft:diamond"), 50);
        int moved = 0;
        for (int i = 0; i < 50; ++i) {
            moved += countIn(i % 10, 0, (i / 10) * 2, "qtcraft:diamond");
        }
        QCOMPARE(moved, 50);
        QCOMPARE(m_server->hopperQueue().cooldownTicks, 20);

        chat(u"/limits"_s);
        QVERIFY(m_client->chatLines.join(u'\n').contains(u"hopperBudget = 10"_s));
    }

    void limitsOfOlderStagesTakeEffect()
    {
        // Lowered bounds reach the parts that keep them: items awake, falling blocks, containers.
        QcServerConfig config;
        config.limits.itemsAwake = 20;
        config.limits.fallingBlocks = 16;
        config.limits.maxContainers = 16;
        buildArea(config);
        for (quint32 i = 0; i < 30; ++i) {
            m_server->items().spawnScattered(stack("qtcraft:stick", 1), {at(5, 1, 5).x + 0.5, double(at(5, 1, 5).y), at(5, 1, 5).z + 0.5}, 800000 + i);
        }
        QCOMPARE(m_server->items().activeCount(), 20);
        QVERIFY(!m_server->fallingBlocks().isFull());
        for (quint32 i = 0; i < 16; ++i) {
            m_server->fallingBlocks().start(at(1, 6, 1).offset(0, 0, 0), id("qtcraft:sand"), 900000 + i);
        }
        QVERIFY(m_server->fallingBlocks().isFull());
        QCOMPARE(m_server->blockEntities().limits().containers, 16);
    }

    void singlePlayerWorldUsesWorldJson()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config);
        QVERIFY(m_server->save().has_value());
        cleanup();

        // The world's own limits.
        QcWorldStorage storage(directory.path(), m_content);
        QcResult<QcWorldInfo> info = storage.readInfo();
        QVERIFY(info.has_value());
        info->limits = QJsonObject{{u"hopperBudget"_s, 7}, {u"signalBudget"_s, 1}};
        QVERIFY(storage.writeInfo(*info).has_value());
        join(config);
        QCOMPARE(m_server->config().limits.hopperBudget, 7);
        QCOMPARE(m_server->config().limits.signalBudget, 256);
        cleanup();

        // A dedicated server's server.json decides, the world's are ignored.
        config.limitsFromServer = true;
        join(config);
        QCOMPARE(m_server->config().limits.hopperBudget, 4096);
    }
};

QTEST_GUILESS_MAIN(TestSimulationLimits)
#include "test_simulation_limits.moc"
