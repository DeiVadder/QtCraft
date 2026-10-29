// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcDayCycle.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobManager.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <QTest>

#include <optional>

using namespace Qt::StringLiterals;

namespace {

constexpr double dt = 0.05;
constexpr double noon = QcDayCycle::dayLength * 0.25;

} // namespace

// How creatures leave the store of QcMobManager (decision E-422): one notification per creature
// with its reason, the store readable by the listeners while creatures leave, subscriptions that end.
class TestMobDepartures : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    quint32 m_nextId = 100;

    std::unique_ptr<QcServerWorld> makeWorld(int radius)
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        for (int x = -radius; x <= radius; ++x) {
            for (int z = -radius; z <= radius; ++z) {
                world->loadChunk({x, z});
            }
        }
        return world;
    }

    // Feet position on the generated surface of a column.
    QcVec3 surface(QcServerWorld &world, int x, int z)
    {
        const QcBlockPos pos{x, 0, z};
        return {x + 0.5, static_cast<double>(world.chunk(pos.chunk())->highestNonAir(pos.localX(), pos.localZ()) + 1), z + 0.5};
    }

    int type(const char *name) const
    {
        const std::optional<int> index = m_content->mobIndex(u"qtcraft:"_s + QString::fromLatin1(name));
        if (!index) {
            QcTestUtil::failCase(QStringLiteral("unknown mob %1").arg(QString::fromLatin1(name)));
            return 0;
        }
        return *index;
    }

private slots:
    void everyDepartureIsAnnouncedOnceWithItsReason()
    {
        // Each way out of the store raises one notification naming the creature and the reason,
        // before the creature is gone (decision E-422).
        using Departure = QcMobManager::Departure;
        auto world = makeWorld(9);
        QcMobManager mobs(m_content, 6);
        QList<std::pair<quint32, Departure>> departures;
        mobs.onDeparture([&](const QcCreature &mob, Departure why) {
            QVERIFY(mobs.find(mob.id) != nullptr);
            departures.append({mob.id, why});
        });
        mobs.spawn(type("pig"), surface(*world, 2, 2), 1);
        mobs.spawn(type("zombie"), surface(*world, 140, 0), 2);
        mobs.spawn(type("zombie"), {0.5, QcWorld::voidLimitY - 10.0, 0.5}, 3);
        mobs.spawn(type("pig"), surface(*world, 4, 4), 4);
        mobs.spawn(type("pig"), surface(*world, 6, 6), 5);
        mobs.spawn(type("pig"), surface(*world, 8, 8), 6);

        // The player at the origin: the zombie 140 m away despawns (beyond 128 m), the one below the
        // world falls into the void.
        mobs.tick(*world, {{7, surface(*world, 0, 0), true}}, noon, dt, [this] { return m_nextId++; });
        QVERIFY(mobs.wound(1, 100.0).has_value());
        QVERIFY(mobs.remove(4));
        mobs.unload([](const QcCreature &mob) { return mob.id == 5; });
        // Hurting a creature that survives, removing one that is not there and unloading nothing
        // announce nothing.
        QVERIFY(!mobs.wound(6, 1.0).has_value());
        QVERIFY(!mobs.remove(4));
        mobs.unload([](const QcCreature &) { return false; });

        const QList<std::pair<quint32, Departure>> expected{
            {2, Departure::Despawned}, {3, Departure::Void}, {1, Departure::Died}, {4, Departure::Removed}, {5, Departure::Unloaded}};
        QCOMPARE(departures, expected);
        QVERIFY(mobs.find(6));
        QVERIFY(!mobs.find(5));
    }

    void listenersReadTheWholeStoreWhileCreaturesLeave()
    {
        // Creatures that leave in one step (despawn in a tick, unloading a chunk) are all still in
        // the store, unchanged, while each departure is announced; the others too.
        using Departure = QcMobManager::Departure;
        auto world = makeWorld(9);
        QcMobManager mobs(m_content, 6);
        const auto ids = [&mobs] {
            QList<quint32> list;
            for (const QcCreature &mob : mobs.mobs()) {
                list.append(mob.id);
            }
            return list;
        };
        mobs.spawn(type("zombie"), surface(*world, 140, 0), 1);
        mobs.spawn(type("pig"), surface(*world, 2, 2), 2);
        mobs.spawn(type("zombie"), surface(*world, 0, 140), 3);
        mobs.spawn(type("pig"), surface(*world, 4, 4), 4);
        mobs.spawn(type("zombie"), surface(*world, -140, 0), 5);
        mobs.spawn(type("pig"), surface(*world, 6, 6), 6);

        QList<quint32> before = ids();
        QList<quint32> heard;
        QStringList problems;
        mobs.onDeparture([&](const QcCreature &mob, Departure) {
            heard.append(mob.id);
            if (ids() != before) {
                problems.append(u"store changed while %1 left"_s.arg(mob.id));
            }
            for (const quint32 id : std::as_const(before)) {
                const QcCreature *found = mobs.find(id);
                if (!found || found->id != id || found->health <= 0.0) {
                    problems.append(u"%1 not readable while %2 left"_s.arg(id).arg(mob.id));
                }
            }
        });

        mobs.tick(*world, {{7, surface(*world, 0, 0), true}}, noon, dt, [this] { return m_nextId++; });
        QCOMPARE(heard, (QList<quint32>{1, 3, 5}));
        QCOMPARE(ids(), (QList<quint32>{2, 4, 6}));

        before = ids();
        heard.clear();
        mobs.unload([](const QcCreature &mob) { return mob.id != 4; });
        QCOMPARE(heard, (QList<quint32>{2, 6}));
        QCOMPARE(ids(), (QList<quint32>{4}));
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(u"\n"_s)));
    }

    void departureListenersUnsubscribe()
    {
        // A token ends a subscription, also from inside the listener; the others keep hearing.
        auto world = makeWorld(8);
        QcMobManager mobs(m_content, 6);
        for (quint32 id = 1; id <= 4; ++id) {
            mobs.spawn(type("pig"), surface(*world, static_cast<int>(id) * 2, 0), id);
        }
        int first = 0;
        int second = 0;
        int once = 0;
        const QcMobManager::DepartureToken firstToken = mobs.onDeparture([&](const QcCreature &, QcMobManager::Departure) { ++first; });
        mobs.onDeparture([&](const QcCreature &, QcMobManager::Departure) { ++second; });
        std::optional<QcMobManager::DepartureToken> onceToken;
        onceToken = mobs.onDeparture([&](const QcCreature &, QcMobManager::Departure) {
            ++once;
            mobs.removeDepartureListener(*onceToken);
        });

        QVERIFY(mobs.remove(1));
        mobs.removeDepartureListener(firstToken);
        QVERIFY(mobs.remove(2));
        mobs.unload([](const QcCreature &) { return true; });
        QCOMPARE(first, 1);
        QCOMPARE(second, 4);
        QCOMPARE(once, 1);
    }
};

QTEST_GUILESS_MAIN(TestMobDepartures)
#include "test_mob_departures.moc"
