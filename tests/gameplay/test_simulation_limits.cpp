#include "QcMachineFixture.h"
#include "QcServerSettings.h"

#include <QJsonDocument>
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
        QStringList warnings;
        QCOMPARE(QcServerLimits::fromJson({}, u"server.json"_s, &warnings), limits);
        QVERIFY(warnings.isEmpty());
        // What a server writes reads back the same.
        QCOMPARE(QcServerLimits::fromJson(limits.toJson(), u"server.json"_s, &warnings), limits);
        QVERIFY(warnings.isEmpty());
    }

    void outOfRangeIsClamped()
    {
        const QJsonObject json{{u"hopperBudget"_s, 0}, {u"signalBudget"_s, 1e12}, {u"hopperCooldownTicks"_s, 2.5},
                               {u"signalMinimumStep"_s, u"many"_s}, {u"noSuchLimit"_s, 3}};
        QStringList warnings;
        const QcServerLimits limits = QcServerLimits::fromJson(json, u"server.json"_s, &warnings);
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
        QcServerConfig config;
        settings->applyTo(config);
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
        QCOMPARE(m_server->circuitState().hoppers.cooldownTicks, 20);

        chat(u"/limits"_s);
        QVERIFY(m_client->chatLines.join(u'\n').contains(u"hopperBudget = 10"_s));
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
