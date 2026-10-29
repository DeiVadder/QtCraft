// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcServerTestFixture.h"
#include "fixtures/QcTestUtil.h"

#include <QElapsedTimer>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// Headless measurement of the entities (stage 31.2): every chunk of a square holds two animals, four
// dropped items and an armour stand, and one changed block keeps it loaded; the time of a server
// tick (with its phases, the client sync among them), of saving and of opening the world again.
// Small in every test run (checks only that the entities come back); QTCRAFT_ENTITY_MEASURE=full
// measures the full size and prints MEASURE lines (Release build).
class TestEntityMeasure : public QcServerTestFixture
{
    Q_OBJECT

private:
    struct Size
    {
        int radius = 1;
        int ticks = 20;
    };

    static Size size()
    {
        const int ticks = qEnvironmentVariableIntValue("QTCRAFT_ENTITY_MEASURE_TICKS");
        Size size = qEnvironmentVariable("QTCRAFT_ENTITY_MEASURE") == u"full"_s ? Size{8, 200} : Size{1, 20};
        size.ticks = ticks > 0 ? ticks : size.ticks;
        return size;
    }

    quint32 m_nextId = 3'000'000;

    void furnishChunk(QcChunkPos chunk)
    {
        const std::shared_ptr<const QcChunk> loaded = m_server->world().loadChunk(chunk);
        const int x = chunk.x * QcWorld::chunkSize;
        const int z = chunk.z * QcWorld::chunkSize;
        const int ground = loaded->highestNonAir(4, 4) + 1;
        QVERIFY(m_server->world().setBlock({x, ground + 20, z}, id("qtcraft:stone")));

        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        m_server->mobs().spawn(pig, {x + 4.5, static_cast<double>(ground), z + 4.5}, m_nextId++, false, true);
        m_server->mobs().spawn(pig, {x + 10.5, static_cast<double>(ground), z + 10.5}, m_nextId++, false, true);
        for (int i = 0; i < 4; ++i) {
            QcItemEntityManager::Drop drop;
            drop.stack = {item(i % 2 == 0 ? "qtcraft:stick" : "qtcraft:diamond"), 1 + i, 0};
            drop.position = {x + 2.5 + 3 * i, static_cast<double>(loaded->highestNonAir(2 + 3 * i, 13) + 1), z + 13.5};
            m_server->items().spawn(drop, m_nextId++);
        }

        QcArmourStand stand;
        stand.id = m_nextId++;
        stand.motion.position = {x + 8.5, static_cast<double>(loaded->highestNonAir(8, 2) + 1), z + 2.5};
        stand.item = u"qtcraft:armor_stand"_s;
        stand.held = {item("qtcraft:stick"), 1, 0};
        QVERIFY(m_server->stands().add(stand));
    }

    qsizetype entityCount() const
    {
        return m_server->mobs().mobs().size() + m_server->items().items().size() + m_server->stands().stands().size();
    }

    static void report(const char *what, double ms) { qInfo("MEASURE %s %.3f ms", what, ms); }

private slots:
    // Doors and landing blocks ask whether a living body is in the way (QcEntities::overlapsLiving):
    // items, stands and blocks never are, so many of them cost nothing (review of stage 31.2).
    void bodyChecksLookOnlyAtLivingBodies()
    {
        const bool full = qEnvironmentVariable("QTCRAFT_ENTITY_MEASURE") == u"full"_s;
        const int itemCount = full ? 4000 : 400;
        constexpr int checks = 2000;
        QcServerConfig config;
        config.limits.itemsAwake = itemCount;
        config.limits.itemsTotal = itemCount;
        join(config);
        chat(u"/gamerule mobSpawning false"_s);
        m_server->mobs().removeAll();
        const QcVec3 feet = m_client->motion.position;
        const QList<QcAabb> door{QcAabb{feet + QcVec3{2.0, 0.0, 0.0}, feet + QcVec3{3.0, 2.0, 1.0}}};

        const auto measure = [&] {
            QElapsedTimer timer;
            timer.start();
            int hits = 0;
            for (int i = 0; i < checks; ++i) {
                hits += std::as_const(m_server->entities()).overlapsLiving(door) ? 1 : 0;
            }
            return std::pair{static_cast<double>(timer.nsecsElapsed()) / checks / 1e3, hits};
        };
        const auto [empty, emptyHits] = measure();
        QCOMPARE(emptyHits, 0);

        // Items fill the door's cells: they never block it.
        for (int i = 0; i < itemCount; ++i) {
            QcItemEntityManager::Drop drop;
            drop.stack = {item("qtcraft:stick"), 1, 0};
            drop.position = feet + QcVec3{2.5, 0.25 * (i % 7), 0.5};
            m_server->items().spawn(drop, m_nextId++);
        }
        QCOMPARE(m_server->items().items().size() + m_server->items().queued().size(), qsizetype(itemCount));
        const auto [withItems, itemHits] = measure();
        QCOMPARE(itemHits, 0);
        qInfo("MEASURE body-check-empty %.3f us", empty);
        qInfo("MEASURE body-check-items %.3f us", withItems);
        if (full) {
            QVERIFY2(withItems < 2.0 * empty + 0.5, qPrintable(u"%1 us with items, %2 us without"_s.arg(withItems).arg(empty)));
        }

        // A creature in the door blocks it.
        m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:pig"_s), feet + QcVec3{2.5, 0.0, 0.5}, m_nextId++, false, true);
        QCOMPARE(measure().second, checks);
    }

    void tickSyncSaveAndOpen()
    {
        const Size measure = size();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        m_viewDistance = 4;
        join(config);
        chat(u"/gamerule mobSpawning false"_s);
        const QcChunkPos centre = m_feet.chunk();
        for (int dx = -measure.radius; dx <= measure.radius; ++dx) {
            for (int dz = -measure.radius; dz <= measure.radius; ++dz) {
                furnishChunk({centre.x + dx, centre.z + dz});
            }
        }
        ticks(5);

        QElapsedTimer timer;
        qint64 tickNs = 0;
        std::array<qint64, QcTickPhases::count> phaseNs{};
        for (int i = 0; i < measure.ticks; ++i) {
            timer.start();
            m_server->tick();
            tickNs += timer.nsecsElapsed();
            for (int phase = 0; phase < QcTickPhases::inTick; ++phase) {
                phaseNs[static_cast<std::size_t>(phase)] += m_server->tickPhases().lastTick()[static_cast<std::size_t>(phase)];
            }
            QcTestUtil::pump();
        }
        report("tick", static_cast<double>(tickNs) / measure.ticks / 1e6);
        for (int phase = 0; phase < QcTickPhases::inTick; ++phase) {
            qInfo("MEASURE phase-%s %.3f ms", QcTickPhases::ids[static_cast<std::size_t>(phase)],
                static_cast<double>(phaseNs[static_cast<std::size_t>(phase)]) / measure.ticks / 1e6);
        }
        qInfo("MEASURE entities %lld", static_cast<qlonglong>(entityCount()));

        timer.start();
        QVERIFY(m_server->save().has_value());
        report("first-save", static_cast<double>(timer.nsecsElapsed()) / 1e6);
        ticks(1);
        timer.start();
        QVERIFY(m_server->save().has_value());
        report("second-save", static_cast<double>(timer.nsecsElapsed()) / 1e6);
        const qsizetype animals = m_server->mobs().mobs().size();
        const qsizetype stands = m_server->stands().stands().size();
        const qsizetype items = m_server->items().items().size() + m_server->items().queued().size();

        m_client.reset();
        m_server.reset();
        timer.start();
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        report("open", static_cast<double>(timer.nsecsElapsed()) / 1e6);
        // Chunks are read when needed (stage 31.3): reading all of them is measured on its own.
        timer.start();
        loadStored();
        report("load-all", static_cast<double>(timer.nsecsElapsed()) / 1e6);
        const qsizetype animalsAfter = m_server->mobs().mobs().size();
        const qsizetype standsAfter = m_server->stands().stands().size();
        const qsizetype itemsAfter = m_server->items().items().size() + m_server->items().queued().size();
        QCOMPARE(animalsAfter, animals);
        QCOMPARE(standsAfter, stands);
        QCOMPARE(itemsAfter, items);
    }
};

QTEST_GUILESS_MAIN(TestEntityMeasure)
#include "test_entity_measure.moc"
