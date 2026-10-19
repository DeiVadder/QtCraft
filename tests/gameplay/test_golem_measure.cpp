#include "common/world/QcDayCycle.h"
#include "QcTestUtil.h"
#include "server/core/QcServer.h"
#include "world/QcVillageFixture.h"

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

// What summoning iron golems costs (decision E-470): a fixture village of the plains style (every
// building twice) lives through whole days on a server without players, ticked by hand as fast as it
// goes. Its
// villagers sleep at night and work by day, so they gossip and summon golems by themselves; the
// golems stroll about the village. The same days run once with the golem limit at 0 (no golems)
// and once at the default, alternating, and the time per tick and the golems per day are printed.
// A measurement, not a check beyond "golems come": set QTCRAFT_GOLEM_MEASURE=<days> for the recorded
// run (Debug build, relative); without it one day runs.
class TestGolemMeasure : public QObject
{
    Q_OBJECT

private:
    struct Run
    {
        double msPerTick = 0.0;
        qsizetype golems = 0;
        qsizetype creatures = 0;
    };

    static Run live(const std::shared_ptr<QcContentRegistry> &content, double days, qint64 golemLimit)
    {
        QcVillageFixture::Options options;
        options.houseCopies = 2;
        const std::shared_ptr<const QcVillagePlan> plan = QcVillageFixture::plan(*content, options);
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        config.keepChunksLoaded = true;
        config.limits.villageGolems = golemLimit;
        config.limitsFromServer = true;
        QcServer server(config, content, std::make_unique<QcVillageFixture::Terrain>(content, QList{plan}));
        if (!server.openWorld()) {
            return {};
        }
        const QList<QcChunkPos> chunks = QcVillageFixture::chunksOf(*plan, 2);
        for (const QcChunkPos &chunk : chunks) {
            server.world().loadChunk(chunk);
        }

        // From the evening on: everybody sleeps, then works, then meets at the bell.
        server.setWorldTime(QcDayCycle::dayLength * 10.6);
        const int ticks = int(days * QcDayCycle::dayLength / QcPhysics::tickDuration);
        QElapsedTimer timer;
        timer.start();
        for (int tick = 0; tick < ticks; ++tick) {
            server.tick();
        }
        const double msPerTick = double(timer.nsecsElapsed()) / 1e6 / ticks;
        const int golemType = *content->mobIndex(u"qtcraft:iron_golem"_s);
        const QList<QcCreature> &mobs = server.mobs().mobs();
        return {msPerTick, std::ranges::count(mobs, golemType, &QcCreature::type), mobs.size()};
    }

private slots:
    void golemsOverDays()
    {
        const bool full = qEnvironmentVariableIsSet("QTCRAFT_GOLEM_MEASURE");
        const double days = full ? std::max(1.0, qEnvironmentVariable("QTCRAFT_GOLEM_MEASURE").toDouble()) : 1.0;
        const int rounds = full ? 3 : 1;
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();

        QList<double> without;
        QList<double> with;
        Run last;
        for (int round = 0; round < rounds; ++round) {
            without.append(live(content, days, 0).msPerTick);
            last = live(content, days, 256);
            with.append(last.msPerTick);
        }
        std::ranges::sort(without);
        std::ranges::sort(with);
        qInfo().noquote() << u"%1 days, %2 rounds: tick without golems %3 ms, with %4 ms (median); %5 golems, %6 creatures at the end"_s
                                 .arg(days)
                                 .arg(rounds)
                                 .arg(without.at(rounds / 2), 0, 'f', 4)
                                 .arg(with.at(rounds / 2), 0, 'f', 4)
                                 .arg(last.golems)
                                 .arg(last.creatures);
        // Villagers that slept and worked gossip: a golem comes within the first day.
        QVERIFY(last.golems >= 1);
    }
};

QTEST_GUILESS_MAIN(TestGolemMeasure)
#include "test_golem_measure.moc"
