// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcMachineFixture.h"
#include "server/entities/QcEntities.h"
#include "server/entities/QcEntityChecks.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// The entity register (stage 31.2, decisions E-364 to E-366): every kind rests while its chunk is not
// loaded and wakes with it, crosses chunk borders, survives a save in the middle of a motion without
// being lost or copied; pistons push every body, falling blocks included; resting stands and items are
// not sent to clients.
class TestEntityModel : public QcMachineFixture
{
    Q_OBJECT

public:
    // Entities leave and come back with their chunks here.
    TestEntityModel() { m_unloadChunks = true; }

private:
    quint32 m_nextId = 4'000'000;

    struct Census
    {
        qsizetype creatures = 0;
        qsizetype items = 0;
        qsizetype falling = 0;
        qsizetype stands = 0;
        QSet<quint32> ids;

        bool operator==(const Census &) const = default;
        // The kinds whose number of entities differs from `other` (ids may differ); empty when none.
        QString countDifferences(const Census &other) const
        {
            QStringList differences;
            for (const auto &[kind, count, expected] : {std::tuple{"creatures", creatures, other.creatures}, std::tuple{"items", items, other.items},
                                                        std::tuple{"falling blocks", falling, other.falling},
                                                        std::tuple{"stands", stands, other.stands}}) {
                if (count != expected) {
                    differences.append(u"%1: %2 instead of %3"_s.arg(QLatin1StringView(kind)).arg(count).arg(expected));
                }
            }
            return differences.join(u", "_s);
        }
    };

    Census census() const
    {
        Census counted;
        counted.creatures = m_server->mobs().mobs().size();
        counted.items = m_server->items().items().size() + m_server->items().queued().size();
        counted.falling = m_server->fallingBlocks().all().size();
        counted.stands = m_server->stands().stands().size();
        std::as_const(m_server->entities()).forEach([&counted](const QcEntity &entity) {
            if (entity.kind != QcEntityKind::Player) {
                counted.ids.insert(entity.id);
            }
        });
        return counted;
    }

    static QList<QcVec3> sortedByPlace(QList<QcVec3> places)
    {
        std::ranges::sort(places, [](const QcVec3 &a, const QcVec3 &b) { return std::tuple{a.x, a.y, a.z} < std::tuple{b.x, b.y, b.z}; });
        return places;
    }

    // Where the entities of these kinds are, sorted.
    QList<QcVec3> sortedPlaces(const QList<QcEntityKind> &kinds) const
    {
        QList<QcVec3> sorted;
        std::as_const(m_server->entities()).forEach([&](const QcEntity &entity) {
            if (kinds.contains(entity.kind)) {
                sorted.append(entity.motion.position);
            }
        });
        return sortedByPlace(sorted);
    }

    // Where every entity but the player is, by id.
    QHash<quint32, QcVec3> places() const
    {
        QHash<quint32, QcVec3> result;
        std::as_const(m_server->entities()).forEach([&result](const QcEntity &entity) {
            if (entity.kind != QcEntityKind::Player) {
                result.insert(entity.id, entity.motion.position);
            }
        });
        return result;
    }

    bool allRest(bool resting) const
    {
        bool all = true;
        std::as_const(m_server->entities()).forEach([&all, resting](const QcEntity &entity) {
            if (entity.kind != QcEntityKind::Player && entity.dormant != resting) {
                all = false;
            }
        });
        return all;
    }

    QcVec3 ground(QcChunkPos chunk, int x, int z) const
    {
        const std::shared_ptr<const QcChunk> loaded = m_server->world().chunk(chunk);
        return {chunk.x * QcWorld::chunkSize + x + 0.5, static_cast<double>(loaded->highestNonAir(x, z) + 1), chunk.z * QcWorld::chunkSize + z + 0.5};
    }

    void teleport(const QcVec3 &to)
    {
        chat(u"/tp %1 %2 %3"_s.arg(to.x).arg(to.y).arg(to.z));
    }

    void loadAround(QcChunkPos centre)
    {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                m_server->world().loadChunk({centre.x + dx, centre.z + dz});
            }
        }
    }

    // A creature, a resting item, an item thrown across the east border, a stand and a block
    // falling from high up, all in `chunk`.
    void populate(QcChunkPos chunk)
    {
        m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:pig"_s), ground(chunk, 4, 4), m_nextId++, false, true);
        QcItemEntityManager::Drop resting;
        resting.stack = stack("qtcraft:stick", 5);
        resting.position = ground(chunk, 8, 8);
        m_server->items().spawn(resting, m_nextId++);
        QcItemEntityManager::Drop thrown;
        thrown.stack = stack("qtcraft:diamond", 2);
        thrown.position = ground(chunk, 15, 8) + QcVec3{0.3, 1.5, 0.0};
        thrown.velocity = {5.0, 2.0, 0.0};
        m_server->items().spawn(thrown, m_nextId++);
        QcArmourStand stand;
        stand.id = m_nextId++;
        stand.motion.position = ground(chunk, 12, 4);
        stand.item = u"qtcraft:armor_stand"_s;
        stand.held = stack("qtcraft:stick", 1);
        QVERIFY(m_server->stands().add(stand));
        const QcVec3 high = ground(chunk, 6, 12) + QcVec3{0.0, 30.0, 0.0};
        m_server->fallingBlocks().start(QcPhysics::blockContaining(high), id("qtcraft:gravel"), m_nextId++);
    }

    // Where the entity `entityId` is; none when it is gone.
    std::optional<QcVec3> positionOf(quint32 entityId) const
    {
        std::optional<QcVec3> found;
        std::as_const(m_server->entities()).forEach([&found, entityId](const QcEntity &entity) {
            if (entity.id == entityId) {
                found = entity.motion.position;
            }
        });
        return found;
    }

    // Sand blocks on the floor of the area.
    int sandOnTheFloor() const
    {
        int sand = 0;
        for (int x = 0; x < areaSize; ++x) {
            for (int z = 0; z < areaSize; ++z) {
                sand += isBlock(x, 0, z, "sand") ? 1 : 0;
            }
        }
        return sand;
    }

    // Saves the world, stops the server and opens the world again in a new one (without a client);
    // false when saving or opening failed.
    [[nodiscard]] bool reopen(const QcServerConfig &config)
    {
        if (!m_server->save().has_value()) {
            return false;
        }
        m_client.reset();
        m_server.reset();
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        return m_server->openWorld().has_value();
    }

    void rulesHold() const
    {
        const QStringList problems = QcEntityChecks::problems(m_server->entities());
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(u"; "_s)));
    }

private slots:
    void pistonsPushFallingBlocksInEveryRotation_data()
    {
        QTest::addColumn<int>("turns");
        for (int turns = 0; turns < 4; ++turns) {
            QTest::addRow("turns %d", turns) << turns;
        }
    }

    // Deliberate change of the entity model: a falling block is a body like any other. One in front
    // of a piston is pushed on and lands one cell further; before, it stayed inside the head and broke
    // into an item.
    void pistonsPushFallingBlocksInEveryRotation()
    {
        QFETCH(int, turns);
        m_turns = turns;
        buildArea();
        put(1, 0, 6, u"lever[attach=floor]"_s);
        put(2, 0, 6, u"signal_piston[facing=east]"_s);
        ticks(2);
        m_server->fallingBlocks().start(at(3, 0, 6), id("qtcraft:sand"), m_nextId++);
        flip(1, 0, 6);
        ticks(1);

        QVERIFY2(isBlock(4, 0, 6, "sand"), qPrintable(look(4, 0, 6)));
        QVERIFY2(look(3, 0, 6).startsWith(u"signal_piston_head"_s), qPrintable(look(3, 0, 6)));
        QVERIFY(m_server->fallingBlocks().all().isEmpty());
        QCOMPARE(droppedCount(item("qtcraft:sand")), 0);
        rulesHold();
        m_client.reset();
        m_server.reset();
    }

    // A block pushed sideways while it falls keeps falling from where the push left it, off the
    // centre of its cell, and lands in the cell below its middle: once, without breaking into items.
    void aFallingBlockPushedInMidAirLandsWhereItIs()
    {
        // Unturned, so the piston pushes towards +x in world coordinates.
        m_turns = 0;
        buildArea();
        for (int y = 0; y <= 2; ++y) {
            put(1, y, 6, u"stone"_s);
            put(2, y, 6, u"stone"_s);
        }
        put(1, 3, 6, u"lever[attach=floor]"_s);
        put(2, 3, 6, u"signal_piston[facing=east]"_s);
        ticks(2);

        // Started above the head's cell, a little east and south of its centre.
        const quint32 block = m_nextId++;
        m_server->fallingBlocks().start(at(3, 6, 6), id("qtcraft:sand"), block);
        const QcBlockPos head = at(3, 3, 6);
        m_server->entities().forEach([block](QcEntity &entity) {
            if (entity.id == block) {
                entity.motion.position += QcVec3{0.2, 0.0, 0.3};
            }
        });
        const auto position = [this, block] { return positionOf(block); };

        // Flipped when the block passes the head's cell.
        int waited = 0;
        while (position() && position()->y >= head.y + 0.5 && waited < 40) {
            ticks(1);
            ++waited;
        }
        const std::optional<QcVec3> before = position();
        QVERIFY(before.has_value());
        const double startX = before->x;
        flip(1, 3, 6);
        ticks(1);
        const std::optional<QcVec3> pushed = position();
        QVERIFY2(pushed.has_value(), "the falling block was lost in the push");
        const double pushedBy = pushed->x - startX;
        QVERIFY2(pushedBy > 0.5, qPrintable(QString::number(pushedBy)));
        const double offCentre = std::abs(pushed->x - std::floor(pushed->x) - 0.5) + std::abs(pushed->z - std::floor(pushed->z) - 0.5);
        QVERIFY2(offCentre > 0.1, qPrintable(u"%1 %2"_s.arg(pushed->x).arg(pushed->z)));
        const bool midAir = pushed->y > head.y - 1.0;
        QVERIFY2(midAir, qPrintable(QString::number(pushed->y)));
        const QcBlockPos below = QcPhysics::blockContaining(*pushed);
        rulesHold();

        ticks(60);
        QVERIFY(m_server->fallingBlocks().all().isEmpty());
        const QcBlockPos landed{below.x, at(0, 0, 0).y, below.z};
        const QString there = m_content->block(*m_server->world().blockAt(landed)).name;
        QCOMPARE(there, u"qtcraft:sand"_s);
        QCOMPARE(droppedCount(item("qtcraft:sand")), 0);
        QCOMPARE(sandOnTheFloor(), 1);
        rulesHold();
        m_client.reset();
        m_server.reset();
    }

    void everyKindRestsWakesCrossesBordersAndSurvivesASaveInMotion()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        join(config);
        chat(u"/gamerule mobSpawning false"_s);
        m_server->mobs().removeAll();
        const QcChunkPos far{m_feet.chunk().x + 8, m_feet.chunk().z};
        const QcVec3 home = m_client->motion.position;

        // The player stands in the far chunk; it and its neighbours stay loaded.
        m_server->world().loadChunk(far);
        const QcVec3 spot = ground(far, 1, 1);
        teleport(spot);
        loadAround(far);
        populate(far);
        const Census before = census();
        QCOMPARE(before.ids.size(), 5);
        ticks(5);
        QVERIFY(allRest(false));
        QCOMPARE(census(), before);

        // The thrown item crossed into the next chunk east.
        bool crossed = false;
        for (const QcItemEntity &dropped : m_server->items().items()) {
            crossed = crossed || (dropped.stack.item == item("qtcraft:diamond") && QcEntities::chunkOf(dropped) == QcChunkPos{far.x + 1, far.z});
        }
        QVERIFY(crossed);

        // The player goes home: the chunks unload and everything there goes with them (decision E-374).
        // (The thrown item may still slide in the tick the chunks leave; the stand stands still.)
        const QList<QcVec3> resting = sortedPlaces({QcEntityKind::ArmourStand});
        const QcVec3 fallingWhenLeft = m_server->fallingBlocks().all().constFirst().motion.position;
        teleport(home);
        ticks(2);
        QVERIFY(!m_server->world().chunk(far));
        QVERIFY(census().ids.isEmpty());
        ticks(20);

        // Back again: everything is where it was when the chunks left; the block goes on falling
        // where it stopped.
        teleport(spot);
        loadAround(far);
        QCOMPARE(sortedPlaces({QcEntityKind::ArmourStand}), resting);
        ticks(3);
        QVERIFY(allRest(false));
        QCOMPARE(m_server->fallingBlocks().all().size(), 1);
        QVERIFY(m_server->fallingBlocks().all().constFirst().motion.position.y < fallingWhenLeft.y);
        const QString backDifferences = census().countDifferences(before);
        QVERIFY2(backDifferences.isEmpty(), qPrintable(backDifferences));
        rulesHold();

        // Saved with the block in mid-air and opened again: every entity once, where it was.
        const QHash<quint32, QcVec3> saved = places();
        const QcVec3 fallingAt = m_server->fallingBlocks().all().constFirst().motion.position;
        QVERIFY(reopen(config));
        loadAround(far);
        const Census after = census();
        const QString afterDifferences = after.countDifferences(before);
        QVERIFY2(afterDifferences.isEmpty(), qPrintable(afterDifferences));
        QCOMPARE(after.ids.size(), before.ids.size());
        QCOMPARE(m_server->fallingBlocks().all().constFirst().motion.position, fallingAt);
        QCOMPARE(sortedByPlace(places().values()), sortedByPlace(saved.values()));
        rulesHold();
        m_server.reset();
    }

    // Ids are unique across all kinds and the players after spawning, loading and joining again; the
    // check finds an id given twice and an entity in the wrong table (decision E-364).
    void idsStayUniqueAndTheChecksFindBrokenRules()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        join(config);
        chat(u"/gamerule mobSpawning false"_s);
        chat(u"/summon pig 3"_s);
        chat(u"/summon armor_stand"_s);
        chat(u"/spawnitem stick 5"_s);
        ticks(5);
        rulesHold();
        QVERIFY(m_server->save().has_value());
        m_client.reset();
        m_server.reset();
        join(config);
        ticks(5);
        rulesHold();
        QVERIFY(!m_server->mobs().mobs().isEmpty() && !m_server->stands().stands().isEmpty());

        // An id given twice (the player's) and an item marked as another kind are found.
        const quint32 playerId = m_client->entityId;
        m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:pig"_s), m_client->motion.position, playerId, false, true);
        QStringList problems = QcEntityChecks::problems(m_server->entities());
        QCOMPARE(problems.size(), 1);
        QVERIFY2(problems.constFirst().contains(u"twice"_s), qPrintable(problems.constFirst()));
        m_server->mobs().remove(playerId);
        QVERIFY(!m_server->items().items().isEmpty());
        m_server->items().editableItems().first().kind = QcEntityKind::Creature;
        problems = QcEntityChecks::problems(m_server->entities());
        QCOMPARE(problems.size(), 1);
        QVERIFY2(problems.constFirst().contains(u"table"_s), qPrintable(problems.constFirst()));
        m_server->items().editableItems().first().kind = QcEntityKind::Item;
        rulesHold();
        m_server.reset();
    }

    // A sliding block far from the origin (chunk 0, 0 not loaded) stays visible until its block
    // arrives in the target cell: the client never sees the cell empty (review of stage 31.2).
    void slidingBlocksFarFromTheOriginStayVisible()
    {
        QcServerConfig config;
        config.spawnColumn = std::array<int, 2>{650, 650};
        buildArea(config);
        QVERIFY(!m_server->world().chunk({0, 0}));
        put(1, 0, 6, u"lever[attach=floor]"_s);
        put(2, 0, 6, u"signal_piston[facing=east]"_s);
        put(3, 0, 6, u"stone"_s);
        ticks(3);
        const QcBlockPos target = at(4, 0, 6);
        const QcBlockId stone = id("qtcraft:stone");
        const auto arrived = [&] {
            return std::ranges::any_of(m_client->blockUpdates, [&](QcMsg::BlockUpdate update) { return update.pos == target && update.block == stone; });
        };
        flip(1, 0, 6);
        std::optional<quint32> sliding;
        for (int tick = 0; tick < 6; ++tick) {
            ticks(1);
            for (auto it = m_client->spawns.cbegin(); it != m_client->spawns.cend(); ++it) {
                if (it->kind == QcEntityKind::MovingBlock && it->block == stone) {
                    sliding = it.key();
                }
            }
            const bool shown = (sliding && m_client->entities.contains(*sliding)) || arrived();
            QVERIFY2(shown, qPrintable(u"tick %1"_s.arg(tick)));
        }
        QVERIFY(sliding);
        QVERIFY(arrived());
    }

    // Entities of chunks that load in a frozen world (/tick freeze, decision E-249) wake and are sent
    // although no tick is simulated (review of stage 31.2).
    void entitiesWakeInAFrozenWorld()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        teleport(ground(m_feet.chunk(), 14, 8));
        const QcChunkPos near{m_feet.chunk().x + 2, m_feet.chunk().z};
        m_server->world().loadChunk(near);
        QcItemEntityManager::Drop resting;
        resting.stack = stack("qtcraft:stick", 1);
        resting.position = ground(near, 1, 3);
        const quint32 itemId = m_server->items().spawn(resting, m_nextId++);
        ticks(3);
        QVERIFY(!m_server->world().chunk(near));
        QVERIFY(!m_client->entities.contains(itemId));
        QVERIFY(m_server->items().items().isEmpty());

        // The item comes back with its chunk (with a new id) and is sent although no tick simulates.
        chat(u"/tick freeze"_s);
        m_server->world().loadChunk(near);
        QCOMPARE(m_server->items().items().size(), 1);
        const quint32 back = m_server->items().items().constFirst().id;
        ticks(1);
        QVERIFY(m_client->entities.contains(back));
    }

    // Removing an entity changes only the record of its own chunk: the other regions are not written
    // again (decision E-366).
    void removingAnEntityRewritesOnlyItsRegion()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        join(config);
        chat(u"/gamerule mobSpawning false"_s);
        // Two chunks in two regions, both kept loaded by a changed block; the first one's items are
        // older (earlier in the table).
        const QcChunkPos first{m_feet.chunk().x, m_feet.chunk().z};
        const QcChunkPos second{m_feet.chunk().x + 40, m_feet.chunk().z};
        QList<quint32> firstItems;
        for (const QcChunkPos &chunk : {first, second}) {
            m_server->world().loadChunk(chunk);
            QVERIFY(m_server->world().setBlock(QcPhysics::blockContaining(ground(chunk, 0, 0) + QcVec3{0.0, 20.0, 0.0}), id("qtcraft:stone")));
            for (int i = 0; i < 3; ++i) {
                QcItemEntityManager::Drop drop;
                drop.stack = stack("qtcraft:stick", 1 + i);
                drop.position = ground(chunk, 3 + 3 * i, 5);
                const quint32 spawned = m_server->items().spawn(drop, m_nextId++);
                if (chunk == first) {
                    firstItems.append(spawned);
                }
            }
        }
        ticks(5);
        QVERIFY(m_server->save().has_value());

        const QcChunkPos secondRegion = QcWorldStorage::regionOf(second);
        const QString secondFile =
            QDir(directory.filePath(u"entities"_s)).filePath(u"r.%1.%2.qen"_s.arg(secondRegion.x).arg(secondRegion.z));
        QVERIFY(QFile::exists(secondFile));
        QFile marked(secondFile);
        QVERIFY(marked.open(QIODevice::WriteOnly | QIODevice::Truncate));
        marked.write("untouched");
        marked.close();

        QVERIFY(m_server->items().take(firstItems.constFirst(), 1));
        QVERIFY(m_server->save().has_value());
        QVERIFY(marked.open(QIODevice::ReadOnly));
        QCOMPARE(marked.readAll(), QByteArray("untouched"));
        m_server.reset();
    }

    // Deliberate change of the entity model: stands and items in chunks that are not loaded rest like
    // every other kind and are not sent; they come back when their chunk does.
    void restingStandsAndItemsAreNotSent()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        // At the east edge of the spawn chunk; two chunks east is beyond the streamed ring but
        // within sight of items (32 m).
        teleport(ground(m_feet.chunk(), 14, 8));
        const QcChunkPos near{m_feet.chunk().x + 2, m_feet.chunk().z};
        m_server->world().loadChunk(near);
        QcArmourStand stand;
        stand.id = m_nextId++;
        stand.motion.position = ground(near, 2, 3);
        stand.item = u"qtcraft:armor_stand"_s;
        QVERIFY(m_server->stands().add(stand));
        QcItemEntityManager::Drop resting;
        resting.stack = stack("qtcraft:stick", 1);
        resting.position = ground(near, 1, 3);
        const quint32 itemId = m_server->items().spawn(resting, m_nextId++);
        QVERIFY(itemId != 0);

        // Seen while the chunk is loaded; it unloads at the end of the tick (nobody needs it) and
        // takes them along (decision E-374).
        ticks(1);
        QVERIFY(m_client->entities.contains(stand.id));
        QVERIFY(m_client->entities.contains(itemId));
        QVERIFY(!m_server->world().chunk(near));

        ticks(2);
        QVERIFY(!m_client->entities.contains(stand.id));
        QVERIFY(!m_client->entities.contains(itemId));
        QCOMPARE(m_server->stands().stands().size(), 0);
        QCOMPARE(m_server->items().items().size(), 0);

        m_server->world().loadChunk(near);
        QCOMPARE(m_server->stands().stands().size(), 1);
        QCOMPARE(m_server->items().items().size(), 1);
        const quint32 standBack = m_server->stands().stands().constFirst().id;
        const quint32 itemBack = m_server->items().items().constFirst().id;
        ticks(1);
        QVERIFY(m_client->entities.contains(standBack));
        QVERIFY(m_client->entities.contains(itemBack));
    }
};

QTEST_GUILESS_MAIN(TestEntityModel)
#include "test_entity_model.moc"
