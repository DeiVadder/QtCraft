#include "gameplay/QcEndermanFixture.h"
#include "QcTestUtil.h"
#include "server/gameplay/QcSafeSpot.h"
#include "server/mobs/QcMobSteering.h"
#include "server/projectiles/QcProjectileFlight.h"
#include "server/storage/QcEntityCodec.h"
#include "server/storage/QcEntityRecords.h"

using namespace Qt::StringLiterals;

// The finer rules of endermen and ender pearls (decision E-460): who provokes an enderman and for
// how long, the gaze that narrows with the distance, what it carries and where it puts it, when it
// is kept with the world, teleports that reach the clients at once, pearls that never pass through a
// wall or a roof and none after the thrower died, and spawning only with room for its height. Ticks
// are stepped by hand.
class TestEndermanRules : public QcEndermanFixture
{
    Q_OBJECT

private:
    // A second player joined to the server; its server side is the last client.
    std::unique_ptr<RawClient> joinOther()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        auto other = std::make_unique<RawClient>(std::move(clientEnd));
        other->hello(u"Other"_s);
        QcTestUtil::pump();
        ticks(2);
        return other;
    }

    // Glass all around the cell column of `feet` up to four cells: the creature inside sees nobody.
    void glassAround(QcBlockPos feet)
    {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dy = 0; dy <= 3; ++dy) {
                    if (dx != 0 || dz != 0 || dy == 3) {
                        m_server->world().setBlock(feet.offset(dx, dy, dz), id("qtcraft:glass"));
                    }
                }
            }
        }
    }

    // Saves the chunk of the creature through the entity codec and loads it again; the creature of
    // `kind` that came back.
    const QcCreature *saveAndLoad(quint32 creature, const char *kind)
    {
        const QcChunkPos chunk = QcEntities::chunkOf(*mob(creature));
        const QHash<QcChunkPos, QcEntityRecord> records = QcEntityRecords::take(m_server->entities(), {chunk});
        QcLoadWarnings warnings;
        const QcResult<QcEntityRecord> back = QcEntityCodec::decode(QcEntityCodec::encode(records[chunk], *m_content), chunk, *m_content, warnings);
        QSet<quint32> before;
        for (const QcCreature &creature : m_server->entities().creatures().mobs()) {
            before.insert(creature.id);
        }
        if (!back.has_value() || !QcEntityRecords::apply(*back, m_server->entities()).isEmpty()) {
            return nullptr;
        }
        const auto loaded = std::ranges::find_if(m_server->entities().creatures().mobs(), [&](const QcCreature &creature) {
            return creature.type == type(kind) && !before.contains(creature.id);
        });
        return loaded == m_server->entities().creatures().mobs().end() ? nullptr : &*loaded;
    }

    // Ticks until the creature carries `block` (or no longer carries one for air); false on time-out.
    bool carriesWithin(quint32 creature, QcBlockId block, int maxTicks)
    {
        for (int tick = 0; tick < maxTicks && mob(creature) && mob(creature)->carried != block; ++tick) {
            ticks(1);
        }
        return mob(creature) && mob(creature)->carried == block;
    }

    // Content in which only endermen spawn as hostile creatures, also in caves.
    void onlyEndermenAlsoInCaves()
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        for (const char *other : {"zombie", "skeleton", "slime"}) {
            QcMobDefinition quiet = m_content->mobs()[type(other)];
            quiet.spawnWeight = 0.0;
            quiet.extraSpawns.clear();
            content->registerMob(quiet);
        }
        QcMobDefinition enderman = endermanKind();
        enderman.spawnsInCaves = true;
        content->registerMob(enderman);
        m_content = content;
    }

    // Stone up to a surface of planks at 60 with a cave `height` cells high from 30 everywhere in the
    // chunks of spawned(); open air above.
    void buildCave(QcServerWorld &world, int height) const
    {
        for (int x = -80; x < 96; ++x) {
            for (int z = -80; z < 96; ++z) {
                for (int y = 1; y <= 60; ++y) {
                    const bool open = y >= 30 && y < 30 + height;
                    world.setBlock({x, y, z}, open ? qcAirBlockId : (y == 60 ? id("qtcraft:oak_planks") : id("qtcraft:stone")));
                }
                for (int y = 61; y <= 120; ++y) {
                    world.setBlock({x, y, z}, qcAirBlockId);
                }
            }
        }
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
        m_content = QcTestUtil::loadContent();
    }

    void theAttackerIsChasedNotTheNearest()
    {
        joinArena();
        const std::unique_ptr<RawClient> other = joinOther();
        QcServerClient &nearer = *m_server->entities().clients().back();
        const quint32 enderman = spawn("enderman", 8, 0, true);
        nearer.motion.position = mob(enderman)->motion.position + QcVec3{-2.0, 0.0, 0.0};
        player().motion.position = mob(enderman)->motion.position + QcVec3{-3.5, 0.0, 0.0};
        ticks(1);

        // The raw player strikes it through the game; the other player stands closer.
        send(QcMsg::AttackEntity{enderman});
        ticks(1);
        QVERIFY(mob(enderman)->health < endermanKind().maxHealth);
        QCOMPARE(mob(enderman)->provokedBy, player().id);
        QVERIFY(nearer.id != player().id);
    }

    void damageWithoutAPlayerProvokesNobody()
    {
        joinArena();
        const quint32 enderman = spawn("enderman", 5, 0, true);
        m_server->systems().lightning.strike(QcPhysics::blockContaining(mob(enderman)->motion.position));
        QVERIFY(mob(enderman)->health < endermanKind().maxHealth);
        ticks(2 * QcMobSteering::sightCheckTicks);
        QCOMPARE(mob(enderman)->provokedBy, 0u);
    }

    void angerEndsTwentySecondsAfterItLastSawTheTarget()
    {
        joinArena();
        const quint32 enderman = spawn("enderman", 6, 0, true);
        glassAround(QcPhysics::blockContaining(mob(enderman)->motion.position));
        m_server->entities().creatures().damage({.creature = enderman, .damage = 1.0, .from = player().motion.position, .attacker = player().id});
        ticks(1);
        QCOMPARE(mob(enderman)->provokedBy, player().id);

        // Behind glass it does not see the player: angry for 20 s, then calm.
        const int angerTicks = int(QcNeutralBrain::angerSeconds / dt);
        ticks(angerTicks - 20);
        QCOMPARE(mob(enderman)->provokedBy, player().id);
        ticks(40);
        QCOMPARE(mob(enderman)->provokedBy, 0u);
        QCOMPARE(mob(enderman)->chasing, 0u);
    }

    void anInvulnerablePlayersLookDoesNothing()
    {
        joinArena();
        chat(u"/gamemode creative"_s);
        QVERIFY(player().rules().invulnerable);
        const quint32 enderman = spawn("enderman", 0, -8, true);
        for (int tick = 0; tick < 3 * QcMobSteering::sightCheckTicks; ++tick) {
            lookAt(headOf(enderman));
            ticks(1);
        }
        QCOMPARE(mob(enderman)->provokedBy, 0u);
    }

    void theGazeNarrowsWithTheDistance()
    {
        // High in the open air of loaded chunks: nothing hides the head.
        QcServerWorld world(m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        for (int x = -1; x <= 4; ++x) {
            world.loadChunk({x, 0});
        }
        const QcGazeDefinition &gaze = *endermanKind().gaze;
        const QcVec3 feet{0.5, 200.0, 8.5};
        const QcVec3 eyes = feet + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
        const auto view = [&](const QcVec3 &target) {
            return QcMobManager::PlayerView{.entityId = 1, .position = feet, .alive = true, .targetable = true, .look = (target - eyes).normalized()};
        };

        // From 50 m a look 2.5 m beside the head (2.9°, within the old fixed 5°) leaves it calm, one
        // at the head does not.
        const QcVec3 farHead = eyes + QcVec3{50.0, 0.0, 0.0};
        QVERIFY(QcNeutralBrain::gazedAt(view(farHead), farHead, gaze, world));
        QVERIFY(!QcNeutralBrain::gazedAt(view(farHead + QcVec3{0.0, 0.0, 2.5}), farHead, gaze, world));
        // From 5 m the same 2.9° is close enough.
        const QcVec3 nearHead = eyes + QcVec3{5.0, 0.0, 0.0};
        QVERIFY(QcNeutralBrain::gazedAt(view(nearHead + QcVec3{0.0, 0.0, 0.25}), nearHead, gaze, world));
    }

    void onlySingleCellBlocksWithoutBlockEntitiesAreCarried()
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcMobDefinition odd = endermanKind();
        odd.name = u"test:odd"_s;
        odd.carries->blocks = {u"qtcraft:chest"_s, u"qtcraft:furnace"_s, u"qtcraft:oak_door"_s, u"qtcraft:bed"_s, u"qtcraft:oak_sapling"_s,
                               u"qtcraft:dirt"_s};
        content->registerMob(odd);
        const QStringList warnings = content->finish();
        for (const char *name : {"chest", "furnace", "oak_door", "bed", "oak_sapling"}) {
            const QString expected = u"mob 'test:odd': block 'qtcraft:%1' has a block entity or more than one cell and is not carried"_s.arg(
                QString::fromLatin1(name));
            QVERIFY2(warnings.contains(expected), qPrintable(warnings.join(u'\n')));
        }
        const QcCarryDefinition &carry = *content->mobs()[*content->mobIndex(u"test:odd"_s)].carries;
        QCOMPARE(carry.blockIds, QList<QcBlockId>{id("qtcraft:dirt")});
    }

    void aFlowerIsPutDownOnlyWhereItHolds()
    {
        setCarryChances(0.0, 1.0);
        joinArena();
        for (int dx = -arena; dx <= arena; ++dx) {
            for (int dz = -arena; dz <= arena; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:stone"));
            }
        }
        const quint32 enderman = spawn("enderman", 5, 5);
        m_server->entities().creatures().carryBlock(*mob(enderman), id("qtcraft:poppy"));
        ticks(200);
        // On stone a poppy would pop at once: it keeps it.
        QCOMPARE(mob(enderman)->carried, id("qtcraft:poppy"));

        for (int dx = -arena; dx <= arena; ++dx) {
            for (int dz = -arena; dz <= arena; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:grass_block"));
            }
        }
        QVERIFY(carriesWithin(enderman, qcAirBlockId, 400));
    }

    void aCreatureKeptBeforeStaysKept()
    {
        setCarryChances(1.0, 1.0);
        joinArena();
        const quint32 enderman = spawn("enderman", 5, 5);
        m_server->entities().creatures().keep(*mob(enderman), true);
        const quint64 uid = mob(enderman)->uid;
        QVERIFY(carriesWithin(enderman, id("qtcraft:grass_block"), 400));
        QVERIFY(!mob(enderman)->keptForBlock);
        QVERIFY(carriesWithin(enderman, qcAirBlockId, 400));
        QVERIFY(mob(enderman)->persistent);
        QCOMPARE(mob(enderman)->uid, uid);
    }

    void keptForItsBlockAcrossSavingAndLetGoAfterwards()
    {
        setCarryChances(1.0, 1.0);
        joinArena();
        const quint32 enderman = spawn("enderman", 5, 5);
        QVERIFY(carriesWithin(enderman, id("qtcraft:grass_block"), 400));
        QVERIFY(mob(enderman)->persistent && mob(enderman)->keptForBlock);

        // Saved and loaded, it still knows why it is kept; the confined mark comes back too.
        const quint32 penned = spawn("enderman", -20, -20, true);
        m_server->entities().creatures().keep(*mob(penned), true);
        const QcCreature *loaded = saveAndLoad(enderman, "enderman");
        QVERIFY(loaded);
        QVERIFY(loaded->keptForBlock && loaded->persistent);
        QCOMPARE(loaded->carried, id("qtcraft:grass_block"));
        const quint32 carrier = loaded->id;
        const QcCreature *confined = saveAndLoad(penned, "enderman");
        QVERIFY(confined && confined->confined && !confined->keptForBlock);

        // With mobGriefing off it drops the block as an item and is no longer kept.
        chat(u"/gamerule mobGriefing false"_s);
        m_server->entities().creatures().find(carrier)->dormant = false;
        const int before = droppedCount(item("qtcraft:grass_block"));
        QVERIFY(carriesWithin(carrier, qcAirBlockId, 400));
        QCOMPARE(droppedCount(item("qtcraft:grass_block")), before + 1);
        QVERIFY(!mob(carrier)->persistent && !mob(carrier)->keptForBlock);
    }

    void aTeleportByAProjectileReachesTheClientsAtOnce()
    {
        joinArena();
        const quint32 enderman = spawn("enderman", 4, -2);
        ticks(3);
        QVERIFY(m_client->entities.contains(enderman));
        QList<QcMsg::EntitySpawn> spawns;
        m_client->onEntityMessage = [&spawns, enderman](const QcMessage &message) {
            const auto *spawned = std::get_if<QcMsg::EntitySpawn>(&message);
            if (spawned && spawned->id == enderman) {
                spawns.append(*spawned);
            }
        };

        const QcVec3 before = mob(enderman)->motion.position;
        m_server->systems().projectiles.launch({0, u"qtcraft:arrow"_s, {m_feet.x - 1.5, m_feet.y + 1.0, m_feet.z - 1.5}, {40.0, 0.0, 0.0}});
        for (int tick = 0; tick < 20 && (mob(enderman)->motion.position - before).length() < 2.0; ++tick) {
            ticks(1);
        }
        // In the tick of the jump the client got it anew at its new place, not a slide.
        QVERIFY((mob(enderman)->motion.position - before).length() >= 2.0);
        QCOMPARE(spawns.size(), 1);
        QVERIFY((spawns.front().position - mob(enderman)->motion.position).length() < 1e-3);
        m_client->onEntityMessage = {};
    }

    void aPearlNeverPassesThroughAWallOrARoof()
    {
        QcServerWorld world(m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        world.loadChunk({0, 0});
        const int floor = 60;
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = floor - 2; y <= floor + 8; ++y) {
                    // A floor, a roof one block thick at floor + 3, a wall one block thick at x = 10.
                    const bool solid = y < floor || y == floor + 3 || x == 10;
                    world.setBlock({x, y, z}, solid ? id("qtcraft:stone") : qcAirBlockId);
                }
            }
        }

        // Thrown up into the roof: under it, never on top.
        const std::optional<QcVec3> roof = QcSafeSpot::nearest(world, {4.5, floor + 3.05, 4.5}, {0.0, 1.0, 0.0}, 2);
        QCOMPARE(roof, std::optional(QcVec3{4.5, double(floor + 1), 4.5}));
        // Down onto the roof from above: on top of it.
        const std::optional<QcVec3> onRoof = QcSafeSpot::nearest(world, {4.5, floor + 3.95, 4.5}, {0.0, -1.0, 0.0}, 2);
        QCOMPARE(onRoof, std::optional(QcVec3{4.5, double(floor + 4), 4.5}));
        // Into the wall from the west, under the roof: on the west side.
        const std::optional<QcVec3> wall = QcSafeSpot::nearest(world, {10.05, floor + 1.5, 4.5}, {1.0, 0.0, 0.0}, 2);
        QCOMPARE(wall, std::optional(QcVec3{9.5, double(floor + 1), 4.5}));
        // From the east: on the east side.
        const std::optional<QcVec3> east = QcSafeSpot::nearest(world, {10.95, floor + 1.5, 4.5}, {-1.0, 0.0, 0.0}, 2);
        QCOMPARE(east, std::optional(QcVec3{11.5, double(floor + 1), 4.5}));
    }

    void aPearlOfADeadThrowerTeleportsNobody()
    {
        joinArena(noon);
        const QcVec3 eyes = player().motion.position + QcVec3{0.0, 1.5, 0.0};
        m_server->systems().projectiles.launch({player().id, u"qtcraft:ender_pearl"_s, eyes, {6.0, 12.0, 0.0}});
        ticks(2);
        QVERIFY(!m_server->entities().projectiles().all().isEmpty());
        chat(u"/kill"_s);
        QVERIFY(player().dead);
        send(QcMsg::Respawn{});
        QVERIFY(!player().dead);
        const QcVec3 respawned = player().motion.position;

        for (int tick = 0; tick < 100 && !m_server->entities().projectiles().all().isEmpty(); ++tick) {
            ticks(1);
        }
        QVERIFY(m_server->entities().projectiles().all().isEmpty());
        QVERIFY((player().motion.position - respawned).length() < 0.5);
        QCOMPARE(player().health, QcServerClient::maxHealth);
    }

    void noSpawningUnderALowRoof()
    {
        // Only endermen spawn, also in caves, and only on stone (planks carry spawns since the general
        // ground rule, decision E-836): they could only appear in the cave of the world below.
        onlyEndermenAlsoInCaves();
        auto onStone = std::make_shared<QcContentRegistry>(*m_content);
        QcMobDefinition enderman = endermanKind();
        enderman.spawnBlocks = {u"qtcraft:stone"_s};
        onStone->registerMob(enderman);
        m_content = onStone;
        const auto caveOf = [this](int height) { return [this, height](QcServerWorld &world) { buildCave(world, height); }; };
        // Two cells high: none; three: some.
        QVERIFY(spawned("enderman", midnight, 120, caveOf(2)).isEmpty());
        QVERIFY(!m_spawnedInsideBlock);
        QVERIFY(!spawned("enderman", midnight, 120, caveOf(3)).isEmpty());
        QVERIFY(!m_spawnedInsideBlock);
    }
};

QTEST_GUILESS_MAIN(TestEndermanRules)
#include "test_enderman_rules.moc"
