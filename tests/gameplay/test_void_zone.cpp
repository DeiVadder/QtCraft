#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPhysics.h"
#include "QcServerTestFixture.h"
#include "QcTestUtil.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/storage/QcWorldStorage.h"
#include "worldgen/generators/QcWorldGenerators.h"

#include <QTemporaryDir>

#include <algorithm>

using namespace Qt::StringLiterals;

// The void below the world (decision E-288): no invisible floor at the bottom, void damage that
// armour and creative mode do not stop, its death message, and the removal of everything else
// that falls below the void limit.
class TestVoidZone : public QcServerTestFixture
{
    Q_OBJECT

private:
    static QcServerConfig creative()
    {
        QcServerConfig config;
        config.defaultGameMode = QcGameMode::Creative;
        return config;
    }

    // Opens the column under the player down to the bottom of the world.
    void openHole()
    {
        for (int y = m_feet.y - 1; y >= 0; --y) {
            m_server->world().setBlock({m_feet.x, y, m_feet.z}, qcAirBlockId);
        }
    }

    // One input of standing still (flying or not) and one server tick.
    void step(bool flying = false)
    {
        QcPlayerInput input;
        input.sequence = ++m_sequence;
        input.flying = flying;
        m_client->connection.send(QcMsg::PlayerInput{input});
        QcTestUtil::pump();
        ticks(1);
    }

    // Steps until the player is in the void; returns the ticks it took, -1 if it never got there.
    int fallIntoTheVoid(int limit)
    {
        for (int tick = 1; tick <= limit; ++tick) {
            step();
            if (QcWorld::isInVoid(m_client->motion.position.y)) {
                return tick;
            }
        }
        return -1;
    }

    // Health after each further tick until the player dies or `limit` ticks passed.
    QList<double> healthUntilDeath(int limit, bool flying = false)
    {
        QList<double> health{m_client->health};
        for (int tick = 0; tick < limit && !m_client->dead; ++tick) {
            step(flying);
            health.append(m_client->health);
        }
        return health;
    }

    // Health one tick after entering the void and every tick after that: a hit at once, then one
    // every voidDamageIntervalTicks ticks, from full health to death.
    static QList<double> expectedVoidHealth()
    {
        QList<double> health;
        double left = QcServerClient::maxHealth;
        for (int tick = 0; left > 0.0; ++tick) {
            if (tick % QcPlayerHealth::voidDamageIntervalTicks == 0) {
                left = std::max(0.0, left - QcPlayerHealth::voidDamage);
            }
            health.append(left);
        }
        return health;
    }

    void equip(const char *armour)
    {
        chat(u"/give "_s + QString::fromLatin1(armour));
        send(QcMsg::SelectHotbarSlot{slotOf(item(armour))});
        send(QcMsg::ConsumeItem{});
    }

    bool lastChatIs(const char *key, const QString &argument) const
    {
        return !m_client->chatKeys.isEmpty() && m_client->chatKeys.last().key == QLatin1String(key)
               && m_client->chatKeys.last().arguments == QStringList{argument};
    }

    static QcArmourStand standAt(quint32 id, const QcVec3 &position)
    {
        QcArmourStand stand;
        stand.id = id;
        stand.motion.position = position;
        stand.item = u"qtcraft:armor_stand"_s;
        return stand;
    }

private slots:
    void survivalPlayerFallsThroughAHoleAndDiesInTheVoid()
    {
        join();
        m_server->setKeepInventory(false);
        const char *armour[] = {"qtcraft:diamond_helmet", "qtcraft:diamond_chestplate", "qtcraft:diamond_leggings", "qtcraft:diamond_boots"};
        for (const char *piece : armour) {
            equip(piece);
        }
        for (int slot = 0; slot < 4; ++slot) {
            QVERIFY(!m_client->equipment.stacks[std::size_t(slot)].isEmpty());
        }
        chat(u"/give stone 20"_s);
        QCOMPARE(m_client->health, QcServerClient::maxHealth);

        // Through the hole and past the old floor at y = 0 without stopping.
        openHole();
        const int fallTicks = fallIntoTheVoid(400);
        QVERIFY2(fallTicks > 0, qPrintable(QString::number(m_client->motion.position.y)));
        QVERIFY(!m_client->motion.onGround);

        // 4 points at once and every 10 ticks, untouched by full diamond armour, which does not
        // wear down either.
        QList<double> health{m_client->health};
        health.append(healthUntilDeath(100).mid(1));
        QCOMPARE(health, expectedVoidHealth());
        QVERIFY(m_client->dead);
        for (int slot = 0; slot < 4; ++slot) {
            QCOMPARE(m_client->equipment.stacks[std::size_t(slot)].damage, 0);
        }
        QVERIFY(lastChatIs("%1 fell out of the world", u"Raw"_s));

        // Dead, the player takes no more void damage and dies only once.
        const qsizetype lines = m_client->chatKeys.size();
        ticks(30);
        QCOMPARE(m_client->health, 0.0);
        QCOMPARE(m_client->chatKeys.size(), lines);

        // The inventory dropped into the void and is gone.
        QCOMPARE(countOf(item("qtcraft:stone")), 0);
        QCOMPARE(m_server->items().items().size(), 0);
        QCOMPARE(m_server->items().queued().size(), 0);

        // Back at the spawn, out of the void, and no more void damage.
        send(QcMsg::Respawn{});
        QVERIFY(!m_client->dead);
        QVERIFY(m_client->motion.position.y > 0.0);
        QCOMPARE(m_client->health, QcServerClient::maxHealth);
        m_server->world().setBlock(m_feet.offset(0, -1, 0), id("qtcraft:stone"));
        idle(30);
        QCOMPARE(m_client->health, QcServerClient::maxHealth);

        // The rhythm starts afresh: back in the void, the first hit comes at once again.
        chat(u"/tp ~ %1 ~"_s.arg(QcWorld::voidLimitY - 10.0));
        ticks(1);
        QCOMPARE(m_client->health, QcServerClient::maxHealth - QcPlayerHealth::voidDamage);
    }

    void keepInventoryKeepsItThroughADeathInTheVoid()
    {
        join();
        m_server->setKeepInventory(true);
        chat(u"/give stone 20"_s);
        chat(u"/tp ~ %1 ~"_s.arg(QcWorld::voidLimitY - 10.0));
        healthUntilDeath(100);
        QVERIFY(m_client->dead);

        send(QcMsg::Respawn{});
        QCOMPARE(countOf(item("qtcraft:stone")), 20);
        QCOMPARE(m_server->items().items().size(), 0);
    }

    void rejoiningInTheVoidHurtsAtOnce()
    {
        // A player saved in the void (left while falling) is hit in the first tick after joining
        // and dies like any other.
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        join(config);
        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        m_server.reset();

        const QcWorldStorage storage(directory.path(), m_content);
        QcPlayerData player = *storage.readPlayer(u"Raw"_s)->player;
        player.motion.position.y = QcWorld::voidLimitY - 30.0;
        QVERIFY(storage.writePlayer(u"Raw"_s, player));

        join(config);
        QVERIFY(QcWorld::isInVoid(m_client->motion.position.y));
        QCOMPARE(m_client->health, QcServerClient::maxHealth - QcPlayerHealth::voidDamage);
        healthUntilDeath(100);
        QVERIFY(m_client->dead);
        QVERIFY(lastChatIs("%1 fell out of the world", u"Raw"_s));
    }

    void voidDamageStartsAtTheVoidLimit()
    {
        join();
        chat(u"/tp ~ %1 ~"_s.arg(QcWorld::voidLimitY));
        // A tick without input sends the new position; the server moves players only on input.
        ticks(1);
        QCOMPARE(m_client->motion.position.y, QcWorld::voidLimitY);
        QCOMPARE(m_client->health, QcServerClient::maxHealth);

        step();
        QVERIFY(QcWorld::isInVoid(m_client->motion.position.y));
        QCOMPARE(m_client->health, QcServerClient::maxHealth - QcPlayerHealth::voidDamage);

        // /kill still works down there, with its own message.
        chat(u"/kill"_s);
        QVERIFY(m_client->dead);
        QVERIFY(lastChatIs("%1 died", u"Raw"_s));
    }

    void creativePlayersAreNotSafeFromTheVoid_data()
    {
        QTest::addColumn<bool>("flying");
        QTest::newRow("falling") << false;
        QTest::newRow("flying") << true;
    }

    void creativePlayersAreNotSafeFromTheVoid()
    {
        QFETCH(bool, flying);
        join(creative());
        m_server->setKeepInventory(false);
        const QcItemId diamond = item("qtcraft:diamond");
        send(QcMsg::CreativeItem{0, {diamond, 5, 0}});
        const int food = m_client->food;

        if (flying) {
            step(true);
        }
        chat(u"/tp ~ %1 ~"_s.arg(QcWorld::voidLimitY - 10.0));
        const QList<double> health = healthUntilDeath(100, flying);
        QCOMPARE(health.mid(1), expectedVoidHealth());
        QVERIFY(m_client->dead);
        QVERIFY(lastChatIs("%1 fell out of the world", u"Raw"_s));
        if (flying) {
            QCOMPARE(m_client->motion.position.y, QcWorld::voidLimitY - 10.0);
        }

        // Respawning keeps the mode, the frozen hunger and the catalogue items, which never drop.
        ticks(2);
        QCOMPARE(m_server->items().items().size(), 0);
        send(QcMsg::Respawn{});
        QVERIFY(!m_client->dead);
        QCOMPARE(m_client->gameMode, QcGameMode::Creative);
        QCOMPARE(m_client->food, food);
        QCOMPARE(countOf(diamond), 5);
        QVERIFY(m_client->motion.position.y > 0.0);
    }

    void spawnWithoutGroundGetsABlock_data()
    {
        QTest::addColumn<QString>("options");
        QTest::addColumn<QString>("ground");
        QTest::newRow("no platform") << u"platform=qtcraft:glass,radius=0,y=40"_s << u"qtcraft:glass"_s;
        QTest::newRow("air platform") << u"platform=qtcraft:air,radius=2,y=40"_s << u"qtcraft:stone"_s;
    }

    void spawnWithoutGroundGetsABlock()
    {
        // A void world without a platform: the server puts one block under the spawn instead of
        // dropping the player into the void at every respawn (decision E-288).
        QFETCH(QString, options);
        QFETCH(QString, ground);
        join({}, QcWorldGenerators::create(QString::fromLatin1(QcWorldGenerators::empty), m_content, 1, options));
        QCOMPARE(m_feet.y, 41);

        // The fixture's stage and the placed block are cleared: the respawn places it again.
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dz = -3; dz <= 3; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), qcAirBlockId);
            }
        }
        chat(u"/kill"_s);
        send(QcMsg::Respawn{});
        QVERIFY(!m_client->dead);
        QCOMPARE(m_client->motion.position.y, 41.0);
        QCOMPARE(m_server->world().blockAt(m_feet.offset(0, -1, 0)), m_content->blockId(ground));

        idle(40);
        QVERIFY(!m_client->dead);
        QVERIFY(m_client->motion.onGround);
        QCOMPARE(m_client->motion.position.y, 41.0);
    }

    void itemsFallingIntoTheVoidAreRemoved()
    {
        join();
        openHole();
        const QcItemId stone = item("qtcraft:stone");
        const QcVec3 above{m_feet.x + 0.5, 2.0, m_feet.z + 0.5};
        const quint32 falling = m_server->items().spawn({{stone, 3, 0}, above, {}}, 900'001);
        QVERIFY(falling != 0);

        // It falls through the bottom of the world and goes only once it passes the limit (at most
        // three metres per tick at full speed).
        int ticksAlive = 0;
        double lowest = above.y;
        while (ticksAlive < 200 && droppedCount(stone) > 0) {
            lowest = m_server->items().items().first().motion.position.y;
            QVERIFY(!QcWorld::isInVoid(lowest));
            ticks(1);
            ++ticksAlive;
        }
        QCOMPARE(droppedCount(stone), 0);
        QVERIFY2(lowest < QcWorld::voidLimitY + QcPhysics::terminalVelocity * QcPhysics::tickDuration, qPrintable(QString::number(lowest)));

        // Drops in the void never appear, nor wait in the queue.
        const QcVec3 deep{m_feet.x + 0.5, QcWorld::voidLimitY - 1.0, m_feet.z + 0.5};
        QCOMPARE(m_server->items().spawnScattered({stone, 3, 0}, deep, 900'002), 0u);
        QCOMPARE(m_server->items().items().size(), 0);
        QCOMPARE(m_server->items().queued().size(), 0);
    }

    void creaturesInTheVoidAreRemovedWithoutLoot()
    {
        join();
        openHole();
        // A peaceful animal that never despawns otherwise, falling from just above the limit, and
        // a hostile creature placed below it.
        const QcVec3 column{m_feet.x + 0.5, QcWorld::voidLimitY + 2.0, m_feet.z + 0.5};
        const quint32 pig = m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:pig"_s), column, 900'010, false, true);
        const quint32 zombie =
            m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:zombie"_s), column - QcVec3{0.0, 5.0, 0.0}, 900'011);
        QVERIFY(m_server->mobs().find(pig) && m_server->mobs().find(pig)->persistent);
        QVERIFY(m_server->mobs().find(zombie));

        ticks(1);
        QVERIFY(m_server->mobs().find(pig));
        QVERIFY(!m_server->mobs().find(zombie));
        ticks(20);
        QVERIFY(!m_server->mobs().find(pig));
        QCOMPARE(m_server->items().items().size(), 0);
    }

    void unloadedItemsAndCreaturesInTheVoidAreRemoved()
    {
        // Far from every player, in chunks that are not loaded: in the void items, creatures and
        // falling blocks go anyway; the same ones above the world rest where they are.
        join();
        const QcVec3 far{m_feet.x + 2000.5, QcWorld::voidLimitY - 20.0, m_feet.z + 0.5};
        QVERIFY(!m_server->world().chunk(QcPhysics::blockContaining(far).chunk()));
        const QcItemId stone = item("qtcraft:stone");
        quint32 nextId = 900'100;
        m_server->items().restore({{{stone, 3, 0}, far, {}}, {{stone, 4, 0}, far + QcVec3{0.0, 200.0, 0.0}, {}}},
                                  [&nextId] { return nextId++; });
        QCOMPARE(m_server->items().items().size(), 2);
        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        const quint32 deep = m_server->mobs().spawn(pig, far, 900'030, false, true);
        const quint32 highPig = m_server->mobs().spawn(pig, far + QcVec3{0.0, 200.0, 0.0}, 900'031, false, true);

        QcFallingBlock sand;
        sand.block = id("qtcraft:sand");
        sand.motion.position = far;
        QcFallingBlock high = sand;
        high.motion.position = far + QcVec3{0.0, 200.0, 0.0};
        m_server->fallingBlocks().restore({sand, high}, [&nextId] { return nextId++; });

        ticks(2);
        QCOMPARE(m_server->items().items().size(), 1);
        QCOMPARE(m_server->items().items().first().stack.count, 4);
        QCOMPARE(m_server->fallingBlocks().all().size(), 1);
        // Its chunk is asked for (strays load their chunk, decision E-374); once it is there the block
        // falls again, which a busy machine may reach within these two ticks.
        if (!m_server->world().chunk(QcPhysics::blockContaining(high.motion.position).chunk())) {
            QCOMPARE(m_server->fallingBlocks().all().first().motion.position.y, high.motion.position.y);
        }
        QVERIFY(!m_server->mobs().find(deep));
        QVERIFY(m_server->mobs().find(highPig));
    }

    void armourStandsInTheVoidAreRemoved()
    {
        join();
        QVERIFY(m_server->stands().add(standAt(900'020, {m_feet.x + 0.5, QcWorld::voidLimitY - 0.5, m_feet.z + 0.5})));
        QVERIFY(m_server->stands().add(standAt(900'021, {m_feet.x + 0.5, QcWorld::voidLimitY, m_feet.z + 0.5})));

        ticks(1);
        QVERIFY(!m_server->stands().find(900'020));
        QVERIFY(m_server->stands().find(900'021));
        QCOMPARE(m_server->items().items().size(), 0);
    }

    void fallingBlocksDropIntoTheVoid()
    {
        join();
        openHole();
        // Sand on the lowest layer falls too: below it is the void, not a floor.
        const QcBlockPos bottom{m_feet.x, 0, m_feet.z};
        m_server->world().setBlock(bottom, id("qtcraft:sand"));
        // A block update next to it (as from a player) lets it start.
        chat(u"/setblock %1 1 %2 stone"_s.arg(bottom.x).arg(bottom.z));
        chat(u"/setblock %1 1 %2 air"_s.arg(bottom.x).arg(bottom.z));
        ticks(2);
        QCOMPARE(m_server->world().blockAt(bottom), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(m_server->fallingBlocks().all().size(), 1);

        // It falls until the void limit and goes there, without landing or dropping.
        int ticksFalling = 0;
        double lowest = bottom.y;
        while (ticksFalling < 200 && !m_server->fallingBlocks().all().isEmpty()) {
            lowest = m_server->fallingBlocks().all().first().motion.position.y;
            QVERIFY(!QcWorld::isInVoid(lowest));
            ticks(1);
            ++ticksFalling;
        }
        QVERIFY(m_server->fallingBlocks().all().isEmpty());
        QVERIFY2(lowest < QcWorld::voidLimitY + QcPhysics::terminalVelocity * QcPhysics::tickDuration, qPrintable(QString::number(lowest)));
        QCOMPARE(m_server->items().items().size(), 0);
    }

    void gravelBottomLayerCollapsesIntoTheVoid()
    {
        // A flat world on a gravel bottom layer: one broken block starts a collapse of the whole
        // layer in the loaded chunks. It spreads over many ticks within the falling-block limit,
        // every block falls down to the void limit (none vanishes earlier), and it ends.
        join({}, QcWorldGenerators::create(QString::fromLatin1(QcWorldGenerators::flat), m_content, 1, u"gravel,grass_block"_s));
        const QcBlockId gravel = id("qtcraft:gravel");
        const auto gravelLeft = [this, gravel] {
            int count = 0;
            const QList<QcChunkPos> chunks = m_server->world().loadedChunks();
            for (const QcChunkPos chunk : chunks) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    for (int z = 0; z < QcWorld::chunkSize; ++z) {
                        count += m_server->world().chunk(chunk)->block(x, 0, z) == gravel ? 1 : 0;
                    }
                }
            }
            return count;
        };
        const int before = gravelLeft();
        QVERIFY(before >= 9 * QcWorld::chunkSize * QcWorld::chunkSize);

        chat(u"/setblock %1 1 %2 air"_s.arg(m_feet.x + 2).arg(m_feet.z));
        constexpr double lowestBeforeRemoval = QcWorld::voidLimitY + QcPhysics::terminalVelocity * QcPhysics::tickDuration;
        QHash<quint32, double> lastSeen;
        QSet<quint32> started;
        qsizetype mostFalling = 0;
        int tick = 0;
        for (; tick < 2000; ++tick) {
            ticks(1);
            const QList<QcFallingBlock> &falling = m_server->fallingBlocks().all();
            mostFalling = std::max(mostFalling, falling.size());
            QSet<quint32> present;
            for (const QcFallingBlock &block : falling) {
                present.insert(block.id);
                started.insert(block.id);
                lastSeen.insert(block.id, block.motion.position.y);
            }
            for (auto it = lastSeen.begin(); it != lastSeen.end();) {
                if (present.contains(it.key())) {
                    ++it;
                    continue;
                }
                QVERIFY2(it.value() < lowestBeforeRemoval, qPrintable(QString::number(it.value())));
                it = lastSeen.erase(it);
            }
            if (falling.isEmpty() && gravelLeft() < before && m_server->pendingBlockUpdates() == 0) {
                break;
            }
        }

        QVERIFY2(tick < 2000, qPrintable(QString::number(gravelLeft())));
        QCOMPARE(mostFalling, qsizetype(QcFallingBlockManager::maxFalling));
        QCOMPARE(gravelLeft(), 0);
        // Every block of the layer fell as an entity; none went straight away while the manager
        // was full.
        QCOMPARE(started.size(), before);
        QCOMPARE(m_server->items().items().size(), 0);
    }
};

QTEST_MAIN(TestVoidZone)
#include "test_void_zone.moc"
