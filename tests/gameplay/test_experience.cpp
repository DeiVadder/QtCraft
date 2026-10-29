// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcSnapshotBuffer.h"
#include "common/gameplay/QcExperience.h"
#include "fixtures/QcServerTestFixture.h"
#include "fixtures/QcTestUtil.h"
#include "fixtures/QcTestWorld.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// Experience (stage 36, decision E-398): the level curve with the reference's values, orbs flying
// to the player, collected one every two ticks, merging within the world's bound, despawning, the
// death rule with keepInventory and creative mode, /xp, saving the player and the orbs, and the
// client being told. Ticks are stepped by hand.
class TestExperience : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcServerClient &player() { return *m_server->entities().clients().front(); }

    // An orb of `value` at `position` that can be collected at once.
    void orbAt(const QcVec3 &position, int value)
    {
        QcExperienceOrb orb;
        orb.id = m_server->entities().newId();
        orb.value = value;
        orb.motion.position = position;
        QVERIFY(m_server->orbs().add(orb));
    }

    qint64 orbPoints() const { return m_server->orbs().totalPoints(); }

    // Spawns orbs in a 10 x 10 grid with 0.3 m between them, far from the player.
    void orbField(int count)
    {
        for (int i = 0; i < count; ++i) {
            const int row = i / 10;
            orbAt({m_feet.x - 12.0 + (i % 10) * 0.3, static_cast<double>(m_feet.y), m_feet.z + row * 0.3}, 1);
        }
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void levelCurveHasTheReferenceValues()
    {
        QCOMPARE(QcExperience::pointsForNext(0), 7);
        QCOMPARE(QcExperience::pointsForNext(14), 35);
        QCOMPARE(QcExperience::pointsForNext(15), 37);
        QCOMPARE(QcExperience::pointsForNext(29), 107);
        QCOMPARE(QcExperience::pointsForNext(30), 112);
        QCOMPARE(QcExperience::totalForLevel(5), 55);
        QCOMPARE(QcExperience::totalForLevel(16), 352);
        QCOMPARE(QcExperience::totalForLevel(17), 394);
        QCOMPARE(QcExperience::totalForLevel(30), 1395);
        QCOMPARE(QcExperience::totalForLevel(31), 1507);
        QCOMPARE(QcExperience::totalForLevel(32), 1628);

        // The closed forms are the sums of the costs, level by level.
        qint64 sum = 0;
        for (int level = 0; level < 200; ++level) {
            QCOMPARE(QcExperience::totalForLevel(level), sum);
            QCOMPARE(QcExperience::fromTotal(sum), QcExperience(level, 0));
            QCOMPARE(QcExperience::fromTotal(sum + QcExperience::pointsForNext(level) - 1), QcExperience(level, QcExperience::pointsForNext(level) - 1));
            sum += QcExperience::pointsForNext(level);
        }
        QCOMPARE(QcExperience::fromTotal(-5), QcExperience());
        QCOMPARE(QcExperience::fromTotal(qint64(1) << 40).level(), QcExperience::maxLevel);
    }

    void pointsLevelsAndSpending()
    {
        QcExperience experience;
        QCOMPARE(experience.addPoints(6), 0);
        QCOMPARE(experience.addPoints(1), 1);
        QCOMPARE(experience, QcExperience(1, 0));
        QCOMPARE(experience.addPoints(1395 - 7 + 56), 29);
        QCOMPARE(experience, QcExperience(30, 56));
        QCOMPARE(experience.progress(), 0.5);

        // Levels keep the share of the way to the next one; spending needs enough levels.
        experience.addLevels(-10);
        QCOMPARE(experience, QcExperience(20, 31));
        QVERIFY(!experience.spendLevels(21));
        QVERIFY(!experience.spendLevels(-1));
        QVERIFY(experience.spendLevels(20));
        QCOMPARE(experience.level(), 0);
        QCOMPARE(experience.points(), 3);
        QCOMPARE(experience.addPoints(-100), 0);
        QCOMPARE(experience, QcExperience());

        QVERIFY(experience.setPoints(6));
        QVERIFY(!experience.setPoints(7));
        experience.setLevel(QcExperience::maxLevel + 5);
        QCOMPARE(experience.level(), QcExperience::maxLevel);

        // The highest level holds no points, however it is reached.
        QCOMPARE(experience.points(), 0);
        QVERIFY(!experience.setPoints(1));
        QCOMPARE(QcExperience(QcExperience::maxLevel, 50), QcExperience::fromTotal(QcExperience::totalForLevel(QcExperience::maxLevel) + 50));
        QCOMPARE(QcExperience(QcExperience::maxLevel, 50).points(), 0);

        // Setting the level keeps the share of the way, like adding levels (8 of 17 -> 29 of 62).
        QcExperience set(5, 8);
        set.setLevel(20);
        QCOMPARE(set, QcExperience(20, 29));
    }

    void deathDropAndOrbSizes()
    {
        QCOMPARE(QcExperience(0, 5).deathDrop(), 0);
        QCOMPARE(QcExperience(5, 0).deathDrop(), 35);
        QCOMPARE(QcExperience(14, 0).deathDrop(), 98);
        QCOMPARE(QcExperience(15, 0).deathDrop(), 100);
        QCOMPARE(QcExperience(300, 0).deathDrop(), 100);

        QCOMPARE(QcExperience::orbValues(100, 16), (QList<int>{73, 17, 7, 3}));
        QCOMPARE(QcExperience::orbValues(5, 16), (QList<int>{3, 1, 1}));
        QCOMPARE(QcExperience::orbValues(0, 16), QList<int>());
        // Few orbs at most: the last one takes the rest.
        QCOMPARE(QcExperience::orbValues(6, 2), (QList<int>{3, 3}));
    }

    void orbsFlyToThePlayerAndAreCollected()
    {
        join();
        ticks(1);
        QVERIFY(m_client->experience.has_value());
        QCOMPARE(m_client->experience->level, 0);

        // Four metres away, within reach of the pull: it comes and is collected within two seconds.
        const QcVec3 start{m_feet.x + 4.5, m_feet.y + 0.5, m_feet.z + 0.5};
        orbAt(start, 9);
        ticks(1);
        QVERIFY((m_server->orbs().orbs().front().motion.position - start).length() > 0.0);
        for (int i = 0; i < 40 && !m_server->orbs().orbs().isEmpty(); ++i) {
            idle(1);
        }
        QVERIFY(m_server->orbs().orbs().isEmpty());
        QCOMPARE(player().experience, QcExperience(1, 2));
        QCOMPARE(m_client->experience->level, 1);
        QCOMPARE(m_client->experience->points, 2);
        QCOMPARE(m_client->experience->pointsForNext, 9);

        // Beyond the pull an orb stays where it lies.
        const QcVec3 far{m_feet.x + 12.5, m_feet.y + 0.0, m_feet.z + 0.5};
        orbAt(far, 3);
        idle(40);
        QCOMPARE(m_server->orbs().orbs().size(), 1);
        QVERIFY(std::abs(m_server->orbs().orbs().front().motion.position.x - far.x) < 0.01);
    }

    void aPlayerCollectsOneOrbEveryTwoTicks()
    {
        join();
        const QcVec3 feet{m_feet.x + 0.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        for (int i = 0; i < 6; ++i) {
            orbAt(feet + QcVec3{0.0, 0.3, 0.0}, 1);
        }

        ticks(1);
        QCOMPARE(m_server->orbs().orbs().size(), 5);
        ticks(1);
        QCOMPARE(m_server->orbs().orbs().size(), 5);
        ticks(1);
        QCOMPARE(m_server->orbs().orbs().size(), 4);
        ticks(8);
        QVERIFY(m_server->orbs().orbs().isEmpty());
        QCOMPARE(player().experience.total(), 6);

        // A dead player collects nothing.
        chat(u"/kill"_s);
        orbAt(feet + QcVec3{0.0, 0.3, 0.0}, 1);
        ticks(10);
        QCOMPARE(m_server->orbs().orbs().size(), 1);
    }

    void orbsMergeAndStayWithinTheBound()
    {
        QcServerConfig config;
        config.limits.experienceOrbs = 40;
        join(config);
        orbField(40);
        QCOMPARE(m_server->orbs().orbs().size(), 40);

        // Beyond the bound, dropped experience joins the nearest orb: nothing is lost.
        QCOMPARE(m_server->orbs().drop(250, {m_feet.x - 11.0, double(m_feet.y), m_feet.z + 0.5}, [this] { return m_server->entities().newId(); }), 0);
        QCOMPARE(m_server->orbs().orbs().size(), 40);
        QCOMPARE(orbPoints(), 290);

        // Orbs close together merge into few within a merge interval; the points stay.
        ticks(QcExperienceOrbManager::mergeIntervalTicks);
        QVERIFY2(m_server->orbs().orbs().size() <= 12, qPrintable(QString::number(m_server->orbs().orbs().size())));
        QCOMPARE(orbPoints(), 290);
        QVERIFY(m_server->orbs().mergedCount() >= 28);
    }

    // Near their biggest value orbs neither merge nor take overflow beyond it: they stay apart, and
    // the overflow goes to an orb with room (decision E-399).
    void fullOrbsKeepEveryPoint()
    {
        QcServerConfig config;
        config.limits.experienceOrbs = 3;
        join(config);
        constexpr int nearlyFull = QcExperienceOrbManager::maxValue - 1;
        const QcVec3 spot{m_feet.x - 12.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        orbAt(spot, nearlyFull);
        orbAt(spot + QcVec3{0.3, 0.0, 0.0}, 5);
        ticks(QcExperienceOrbManager::mergeIntervalTicks);
        QCOMPARE(m_server->orbs().orbs().size(), 2);
        QCOMPARE(orbPoints(), qint64(nearlyFull) + 5);

        // Full world: 3 points (one orb) do not fit the nearest orb, they join the one beside it.
        orbAt(spot + QcVec3{0.0, 0.0, 4.0}, 1);
        const auto dropAtSpot = [&](int points) { return m_server->orbs().drop(points, spot, [this] { return m_server->entities().newId(); }); };
        const auto values = [this] {
            QList<int> found;
            for (const QcExperienceOrb &orb : m_server->orbs().orbs()) {
                found.append(orb.value);
            }
            std::ranges::sort(found);
            return found;
        };
        QCOMPARE(dropAtSpot(3), 0);
        QCOMPARE(values(), (QList<int>{1, 8, nearlyFull}));

        // One point still fits the nearest; the next goes beside it.
        QCOMPARE(dropAtSpot(1), 0);
        QCOMPARE(values(), (QList<int>{1, 8, QcExperienceOrbManager::maxValue}));
        QCOMPARE(dropAtSpot(1), 0);
        QCOMPARE(values(), (QList<int>{1, 9, QcExperienceOrbManager::maxValue}));
        QCOMPARE(orbPoints(), qint64(QcExperienceOrbManager::maxValue) + 10);
    }

    // Like items (test_items::itemsFloatUpInWater), orbs float up to the surface of water and do
    // not come to rest there.
    void orbsFloatUpInWater()
    {
        QcTestWorld world(*m_content);
        world.fillBox({-8, 63, -8}, {8, 63, 8}, id("qtcraft:stone"));
        world.fillBox({-2, 64, -2}, {2, 67, 2}, id("qtcraft:water"));
        QcExperienceOrbManager orbs(1);
        QcExperienceOrb orb;
        orb.id = 1;
        orb.value = 3;
        orb.motion.position = {0.5, 64.5, 0.5};
        QVERIFY(orbs.add(orb));
        for (int i = 0; i < 100; ++i) {
            orbs.tick(world, {}, QcPhysics::tickDuration, [](quint32, int) {});
        }

        // Floating at the surface: the top water block reaches y = 68.
        const QcExperienceOrb &floating = orbs.orbs().front();
        QVERIFY2(floating.motion.position.y > 67.0, qPrintable(QString::number(floating.motion.position.y)));
        QVERIFY(!floating.resting);
    }

    void orbsDespawnAfterFiveMinutes()
    {
        join();
        orbAt({m_feet.x - 15.5, static_cast<double>(m_feet.y), m_feet.z + 0.5}, 4);
        m_server->orbs().editableOrbs().front().age = QcExperienceOrbManager::despawnSeconds - 0.12;
        ticks(2);
        QCOMPARE(m_server->orbs().orbs().size(), 1);
        ticks(1);
        QVERIFY(m_server->orbs().orbs().isEmpty());
    }

    void deathDropsSevenPointsPerLevelAtMostAHundred()
    {
        QcServerConfig config;
        config.keepInventory = false;
        join(config);
        chat(u"/xp set Raw 5 levels"_s);
        chat(u"/kill"_s);
        QCOMPARE(orbPoints(), 35);
        QCOMPARE(player().experience, QcExperience());

        m_server->orbs().clear();
        m_client->connection.send(QcMsg::Respawn{});
        QcTestUtil::pump();
        ticks(1);
        chat(u"/xp set Raw 40 levels"_s);
        chat(u"/kill"_s);
        QCOMPARE(orbPoints(), 100);
        ticks(1);
        QCOMPARE(m_client->experience->level, 0);
    }

    void keepInventoryAndCreativeKeepTheExperience()
    {
        join();
        QVERIFY(m_server->keepInventory());
        chat(u"/xp set Raw 12 levels"_s);
        chat(u"/kill"_s);
        QCOMPARE(orbPoints(), 0);
        QCOMPARE(player().experience.level(), 12);

        cleanup();
        QcServerConfig config;
        config.keepInventory = false;
        join(config);
        chat(u"/gamemode creative"_s);
        chat(u"/xp set Raw 12 levels"_s);
        chat(u"/kill"_s);
        QVERIFY(m_client->dead);
        QCOMPARE(orbPoints(), 0);
        QCOMPARE(player().experience.level(), 12);
    }

    void xpCommandAddsSetsAndQueries()
    {
        join();
        chat(u"/xp add Raw 10"_s);
        QCOMPARE(player().experience, QcExperience(1, 3));
        QVERIFY(m_client->chatLines.last().contains(u"level 1 and 3 of 9 points"_s));

        chat(u"/xp add Raw 3 levels"_s);
        QCOMPARE(player().experience.level(), 4);
        chat(u"/xp add Raw -2 levels"_s);
        QCOMPARE(player().experience.level(), 2);
        chat(u"/xp set Raw 30 levels"_s);
        QCOMPARE(player().experience.level(), 30);
        chat(u"/xp set Raw 100 points"_s);
        QCOMPARE(player().experience.points(), 100);
        chat(u"/xp set Raw 112 points"_s);
        QVERIFY(m_client->chatLines.last().contains(u"at most 111 points"_s));
        QCOMPARE(player().experience.points(), 100);
        chat(u"/xp query Raw"_s);
        QVERIFY(m_client->chatLines.last().contains(u"(1495 points in all)"_s));

        chat(u"/xp add Nobody 5"_s);
        QVERIFY(m_client->chatLines.last().contains(u"No player named Nobody"_s));
        for (const QString &wrong : {u"/xp take Raw 5"_s, u"/xp add Raw 5 bars"_s, u"/xp add Raw lots"_s, u"/xp set Raw -1"_s, u"/xp add Raw"_s}) {
            chat(wrong);
            QVERIFY2(m_client->chatLines.last().startsWith(u"Usage"_s), qPrintable(wrong));
        }
        QCOMPARE(player().experience, QcExperience(30, 100));

        // The client hears of it at the end of the next step.
        ticks(1);
        QCOMPARE(m_client->experience->level, 30);
        QCOMPARE(m_client->experience->points, 100);
    }

    void xpCommandNeedsAnOperator()
    {
        join(notOperator());
        chat(u"/xp add Raw 10"_s);
        QCOMPARE(player().experience, QcExperience());
    }

    void experienceAndOrbsAreSaved()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        join(config);
        chat(u"/xp set Raw 17 levels"_s);
        chat(u"/xp set Raw 20 points"_s);
        const QcVec3 lying{m_feet.x - 10.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        orbAt(lying, 37);
        ticks(40);
        const QcVec3 saved = m_server->orbs().orbs().front().motion.position;
        QVERIFY(m_server->save().has_value());
        m_server->waitForStorage();
        cleanup();

        join(config);
        QCOMPARE(player().experience, QcExperience(17, 20));
        QCOMPARE(m_client->experience->level, 17);
        QCOMPARE(m_server->orbs().orbs().size(), 1);
        QCOMPARE(m_server->orbs().orbs().front().value, 37);
        QVERIFY((m_server->orbs().orbs().front().motion.position - saved).length() < 0.1);
    }

    void clientsSeeOrbsWithTheirValue()
    {
        join();
        orbAt({m_feet.x - 10.5, static_cast<double>(m_feet.y), m_feet.z + 0.5}, 17);
        ticks(1);
        const quint32 id = m_server->orbs().orbs().front().id;
        QVERIFY(m_client->spawns.contains(id));
        QCOMPARE(m_client->spawns[id].kind, QcEntityKind::ExperienceOrb);
        QCOMPARE(m_client->spawns[id].health, 17.0);

        // Merged into a bigger orb: sent again with the new value.
        orbAt({m_feet.x - 10.3, static_cast<double>(m_feet.y), m_feet.z + 0.5}, 3);
        ticks(QcExperienceOrbManager::mergeIntervalTicks);
        QCOMPARE(m_server->orbs().orbs().size(), 1);
        QCOMPARE(m_client->spawns[id].health, 20.0);
    }

    // An orb and an item dropped from three metres show at their rest height on the client: the
    // server sends a stop sample when they land, so the client's snapshots end at rest instead of
    // continuing the last fall step (decision E-399).
    void restingOrbsAndItemsShowAtTheirRestHeight()
    {
        join();
        std::map<quint32, QcSnapshotBuffer> tracks;
        qint64 lastMs = 0;
        m_client->onEntityMessage = [&](const QcMessage &message) {
            if (const auto *spawn = std::get_if<QcMsg::EntitySpawn>(&message)) {
                tracks[spawn->id].push({static_cast<double>(lastMs), spawn->position, 0.0f, 0.0f}, 50.0);
            } else if (const auto *moves = std::get_if<QcMsg::EntityMoves>(&message)) {
                lastMs = moves->timeMs;
                for (const QcMsg::EntityMove &move : moves->moves) {
                    tracks[move.id].push({static_cast<double>(moves->timeMs), move.position, 0.0f, 0.0f}, 50.0);
                }
            }
        };

        orbAt({m_feet.x - 10.5, m_feet.y + 3.0, m_feet.z + 0.5}, 5);
        QcItemEntityManager::Drop drop;
        drop.stack = {*m_content->itemId(u"qtcraft:iron_ingot"_s), 1, 0};
        drop.position = {m_feet.x - 10.5, m_feet.y + 3.0, m_feet.z + 3.5};
        const quint32 item = m_server->items().spawn(drop, m_server->entities().newId());
        ticks(60);

        const QcExperienceOrb &orb = m_server->orbs().orbs().front();
        QVERIFY(orb.resting);
        QVERIFY(orb.motion.position.y < m_feet.y + 2.0);
        const auto lying = std::ranges::find_if(m_server->items().items(), [item](const QcItemEntity &entity) { return entity.id == item; });
        QVERIFY(lying != m_server->items().items().end());
        QVERIFY(lying->motion.position.y < m_feet.y + 2.0);
        const std::map<quint32, double> rest{{orb.id, orb.motion.position.y}, {item, lying->motion.position.y}};
        for (const auto &[id, y] : rest) {
            QVERIFY(tracks.contains(id));
            const QcSnapshotBuffer &track = tracks[id];
            QVERIFY(track.size() >= 3);
            // Shown past the newest snapshot, where the client extrapolates.
            const double shown = track.at(track.newest().serverMs + QcSnapshotBuffer::extrapolationMs).position.y;
            QVERIFY2(std::abs(shown - y) < 1e-6, qPrintable(QString::number(shown - y)));
        }
    }

    // Many orbs (stage 36 measurement, Performance-Verlauf.md): a mob farm's kill spot where 200
    // zombies dropped 5 points each beside a waiting player, the same spot with the player standing
    // in it collecting, and orbs spread 1.5 m apart over the loaded area (nothing merges). Small in
    // every test run (checks that the points stay); QTCRAFT_EXPERIENCE_MEASURE=full measures 1000
    // orbs and prints MEASURE lines with the Items phase per tick (Release build).
    void measureManyOrbs()
    {
        const bool full = qEnvironmentVariable("QTCRAFT_EXPERIENCE_MEASURE") == u"full"_s;
        const int kills = full ? 200 : 20;
        const int ticksPerCase = full ? 200 : 20;
        QcServerConfig config;
        config.limits.experienceOrbs = 4096;
        join(config);
        const auto newId = [this] { return m_server->entities().newId(); };

        // The kill spot: 10 m from the player, beyond the pull.
        const QcVec3 spot{m_feet.x - 9.5, m_feet.y + 0.5, m_feet.z + 0.5};
        for (int kill = 0; kill < kills; ++kill) {
            for (int orb = 0; orb < 5; ++orb) {
                QcExperienceOrb one;
                one.id = newId();
                one.value = 1;
                one.motion.position = spot + QcVec3{(orb - 2) * 0.1, 0.0, (kill % 5 - 2) * 0.1};
                QVERIFY(m_server->orbs().add(one));
            }
        }
        const qint64 points = orbPoints();
        QCOMPARE(m_server->orbs().orbs().size(), kills * 5);
        measureCase("farm", ticksPerCase);
        QCOMPARE(orbPoints(), points);
        QVERIFY(m_server->orbs().orbs().size() <= 4);

        // The player walks to where the merged orbs lie and collects all.
        player().motion.position = m_server->orbs().orbs().front().motion.position;
        measureCase("collect", ticksPerCase);
        QVERIFY(m_server->orbs().orbs().isEmpty());
        QCOMPARE(player().experience.total(), points);

        m_server->orbs().clear();
        int spread = 0;
        for (int x = 0; x < 30 && spread < kills * 5; ++x) {
            for (int z = 0; z < 30 && spread < kills * 5; ++z) {
                orbAt({m_feet.x - 15.0 + x * 1.5, m_feet.y + 4.0, m_feet.z - 15.0 + z * 1.5}, 1);
                ++spread;
            }
        }
        measureCase("spread", ticksPerCase);
        QVERIFY(m_server->orbs().orbs().size() > spread / 2);
    }

private:
    void measureCase(const char *name, int ticksToRun)
    {
        const int items = static_cast<int>(QcTickPhase::Items);
        qint64 sum = 0;
        qint64 most = 0;
        const qsizetype before = m_server->orbs().orbs().size();
        for (int i = 0; i < ticksToRun; ++i) {
            ticks(1);
            const qint64 ns = m_server->tickPhases().lastTick()[static_cast<std::size_t>(items)];
            sum += ns;
            most = std::max(most, ns);
        }
        qInfo("MEASURE orbs %s: %lld before, %lld after, Items phase mean %.4f ms, max %.4f ms over %d ticks", name, static_cast<qint64>(before),
              static_cast<qint64>(m_server->orbs().orbs().size()), static_cast<double>(sum) / ticksToRun / 1e6, static_cast<double>(most) / 1e6, ticksToRun);
    }
};

QTEST_GUILESS_MAIN(TestExperience)
#include "test_experience.moc"
