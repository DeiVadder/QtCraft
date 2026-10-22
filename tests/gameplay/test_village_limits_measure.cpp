#include "common/world/QcDayCycle.h"
#include "QcServerTestFixture.h"
#include "QcTestUtil.h"
#include "server/pathing/QcPathPlanner.h"
#include "server/spawning/QcNaturalSpawner.h"
#include "server/villages/QcVillages.h"
#include "world/QcVillageFixture.h"

#include <QElapsedTimer>
#include <QHash>

#include <algorithm>
#include <vector>

using namespace Qt::StringLiterals;

// The server at the villagers' limits (decision E-424): 32 fixture villages of five styles with 32
// villagers each (the limits per village and of the world, 1024), every chunk of them loaded, four
// players spread over them and creatures spawning; ticked by hand from the evening (everybody walks
// home) through the night (hostile creatures spawn around the players) into the morning (everybody
// walks to work). Per tick: its time, the path search nodes of villagers and of the other creatures
// out of the shared budget (QcServerLimits pathNodesPerTick) and how long the searches waited in the
// queue, so that villagers' errands starving the hostile creatures of the budget would show. A
// measurement, not a check: set QTCRAFT_VILLAGE_LIMITS_MEASURE=1 and use a Release build. Without
// it the test runs four villages for a few ticks.
class TestVillageLimitsMeasure : public QcServerTestFixture
{
    Q_OBJECT

private:
    static bool full() { return qEnvironmentVariableIsSet("QTCRAFT_VILLAGE_LIMITS_MEASURE"); }

    // Ticks a search waited in the queue, by kind of creature.
    struct Waits
    {
        qint64 searches = 0;
        qint64 ticks = 0;
        int longest = 0;

        void add(int waited)
        {
            ++searches;
            ticks += waited;
            longest = std::max(longest, waited);
        }
        double mean() const { return searches > 0 ? double(ticks) / double(searches) : 0.0; }
    };

    struct Totals
    {
        QList<double> times;
        qint64 nodes = 0;
        qint64 villagerNodes = 0;
        int mostNodes = 0;
        // Ticks whose searches used up the budget (no room left for another full search).
        qint64 budgetUsed = 0;
        Waits villagers;
        Waits others;
    };

    QList<std::shared_ptr<const QcVillagePlan>> m_plans;
    std::vector<std::unique_ptr<RawClient>> m_players;
    // Ticks each queued creature has been waiting so far.
    QHash<quint32, int> m_waiting;

    // A server with `count` fixture villages, their chunks loaded and their villagers moved in, and
    // `players` players spread over them.
    void start(int count, int players)
    {
        static const QStringList styles{u"qtcraft:plains"_s, u"qtcraft:desert"_s, u"qtcraft:snowy"_s, u"qtcraft:taiga"_s, u"qtcraft:swamp"_s};
        QStringList chosen;
        for (int i = 0; i < count; ++i) {
            chosen.append(styles[i % styles.size()]);
        }
        QcVillageFixture::Options options;
        // Enough beds for the limit of 32 per village.
        options.houseCopies = 4;
        options.alwaysLivedIn = true;
        m_plans = QcVillageFixture::spread(*m_content, chosen, 8, options);
        QCOMPARE(m_plans.size(), count);

        QcServerConfig config;
        config.keepChunksLoaded = true;
        config.operators = {u"Raw"_s};
        join(config, std::make_unique<QcVillageFixture::Terrain>(m_content, m_plans));
        chat(u"/gamerule weatherCycle false"_s);
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            const QList<QcChunkPos> chunks = QcVillageFixture::chunksOf(*plan);
            for (const QcChunkPos &chunk : chunks) {
                m_server->world().loadChunk(chunk);
            }
        }
        addPlayers(players);
        ticks(2);
    }

    // Players beside the first one, each at the centre of another village; around each player's
    // village the dark ground where hostile creatures spawn (QcNaturalSpawner::spawnRadiusMax).
    void addPlayers(int players)
    {
        const int ring = int(QcNaturalSpawner::spawnRadiusMax) / QcWorld::chunkSize + 1;
        for (int i = 0; i < players; ++i) {
            const QList<QcChunkPos> around = QcVillageFixture::chunksOf(*m_plans[qsizetype(i) * m_plans.size() / players], ring);
            for (const QcChunkPos &chunk : around) {
                m_server->world().loadChunk(chunk);
            }
        }
        for (int i = 1; i < players; ++i) {
            auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
            m_server->addClient(std::move(serverEnd), m_origin);
            m_players.push_back(std::make_unique<RawClient>(std::move(clientEnd)));
            m_players.back()->hello(u"Player%1"_s.arg(i));
            QcTestUtil::pump();
            ticks(2);
            const QcBlockPos at = m_plans[qsizetype(i) * m_plans.size() / players]->centre;
            chat(u"/tp Player%1 %2 %3 %4"_s.arg(i).arg(at.x).arg(at.y + 2).arg(at.z + 6));
        }
    }

    // Who waited how long for a search that ran this tick; who is still waiting.
    void noteWaits(Totals &totals)
    {
        for (const QcCreature &mob : m_server->mobs().mobs()) {
            const auto it = m_waiting.find(mob.id);
            if (mob.path.queued) {
                if (it == m_waiting.end()) {
                    m_waiting.insert(mob.id, 1);
                } else {
                    ++*it;
                }
            } else if (it != m_waiting.end()) {
                (mob.isVillager() ? totals.villagers : totals.others).add(*it);
                m_waiting.erase(it);
            }
        }
    }

    Totals run(int count)
    {
        Totals totals;
        totals.times.reserve(count);
        const qint64 budget = QcServerLimits{}.pathNodesPerTick;
        QElapsedTimer timer;
        for (int i = 0; i < count; ++i) {
            timer.start();
            m_server->tick();
            totals.times.append(double(timer.nsecsElapsed()) / 1e6);
            const QcPathPlanner &paths = m_server->mobs().paths();
            totals.nodes += paths.nodesLastTick();
            totals.villagerNodes += paths.villagerNodesLastTick();
            totals.mostNodes = std::max(totals.mostNodes, paths.nodesLastTick());
            totals.budgetUsed += paths.nodesLastTick() > budget - QcServerLimits{}.pathNodesPerSearch ? 1 : 0;
            noteWaits(totals);
            if (i % 20 == 0) {
                QcTestUtil::pump();
            }
        }
        std::ranges::sort(totals.times);
        return totals;
    }

    void report(const char *phase, const Totals &totals) const
    {
        double sum = 0.0;
        for (const double time : totals.times) {
            sum += time;
        }
        const auto n = double(std::max<qsizetype>(1, totals.times.size()));
        const qsizetype villagers = std::ranges::count_if(m_server->mobs().mobs(), [](const QcCreature &mob) { return mob.isVillager(); });
        const qsizetype hostiles = std::ranges::count_if(m_server->mobs().mobs(), [this](const QcCreature &mob) { return m_content->mobs()[mob.type].hostile; });
        qInfo("LIMITS %s: villages=%lld villagers=%lld others=%lld (hostile %lld) players=%lld chunks=%lld ticks=%lld; tick mean %.3f p99 %.3f max %.3f ms; "
              "nodes/tick %.0f (villagers %.0f, others %.0f), most %d; budget used up in %.1f %% of ticks; wait villagers %.2f (max %d, "
              "%lld searches), others %.2f (max %d, %lld searches) ticks",
              phase, qint64(m_plans.size()), qint64(villagers), qint64(m_server->mobs().mobs().size() - villagers), qint64(hostiles),
              qint64(m_players.size() + 1),
              qint64(m_server->world().loadedChunkCount()), qint64(totals.times.size()), sum / n, totals.times[qsizetype((n - 1) * 0.99)],
              totals.times.last(), double(totals.nodes) / n, double(totals.villagerNodes) / n, double(totals.nodes - totals.villagerNodes) / n,
              totals.mostNodes, 100.0 * double(totals.budgetUsed) / n, totals.villagers.mean(), totals.villagers.longest,
              totals.villagers.searches, totals.others.mean(), totals.others.longest, totals.others.searches);
    }

private slots:
    void cleanup()
    {
        m_players.clear();
        m_client.reset();
        m_server.reset();
        m_waiting.clear();
    }

    void atTheLimits()
    {
        const bool measure = full();
        start(measure ? 32 : 4, measure ? 4 : 2);
        const qsizetype villagers = std::ranges::count_if(m_server->mobs().mobs(), [](const QcCreature &mob) { return mob.isVillager(); });
        QVERIFY(villagers >= (measure ? 1000 : 100));

        // The evening: everybody walks home (0.55 to 0.65 of the day), the night with hostile
        // creatures spawning around the players (to 0.8), then the dawn and the morning, when
        // everybody gets up and walks to work (to 0.1 of the next day).
        const auto ticksFor = [measure](double share) { return measure ? int(QcDayCycle::dayLength * 20.0 * share) : 40; };
        m_server->setWorldTime(QcDayCycle::dayLength * 10.55);
        report("evening", run(ticksFor(0.1)));
        report("night", run(ticksFor(0.15)));
        report("morning", run(ticksFor(0.3)));
    }
};

QTEST_GUILESS_MAIN(TestVillageLimitsMeasure)
#include "test_village_limits_measure.moc"
