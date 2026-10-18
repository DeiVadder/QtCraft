#include "common/content/parser/QcContentParser.h"
#include "gameplay/QcEndermanFixture.h"
#include "server/gameplay/QcSafeSpot.h"
#include "server/mobs/QcMobSteering.h"
#include "server/mobs/QcNeutralBrain.h"
#include "server/projectiles/QcProjectileFlight.h"
#include "server/storage/QcEntityCodec.h"
#include "server/storage/QcEntityRecords.h"
#include "server/weather/QcWeatherCycle.h"

#include <QElapsedTimer>
#include <QJsonDocument>

#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

// Endermen and ender pearls (decision E-460): provoked by a look at the head, teleports to safe
// places only (never into blocks, liquids or chunks that are not loaded), projectiles that make them
// teleport instead of hurting them, water and rain, blocks taken and placed under the game rule
// mobGriefing and saved while carried, pearls that teleport their thrower with fall damage, and
// natural spawning. Ticks are stepped by hand.
class TestEndermen : public QcEndermanFixture
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
        m_content = loadContent();
    }

    void endermenComeFromData()
    {
        const QcMobDefinition &enderman = endermanKind();
        QCOMPARE(enderman.ai, QcMobDefinition::Ai::Neutral);
        QVERIFY(enderman.hostile && enderman.gaze && enderman.teleport && enderman.carries);
        QVERIFY(enderman.waterDamage > 0.0 && enderman.height > 2.5);
        QVERIFY(enderman.carries->blockIds.contains(id("qtcraft:grass_block")));
        QVERIFY(enderman.teleport->dodgesProjectiles && enderman.teleport->whenWet);
        const QcItemDefinition &pearl = m_content->item(item("qtcraft:ender_pearl"));
        QCOMPARE(pearl.maxStack, 16);
        QVERIFY(pearl.throwable && pearl.throwable->cooldownSeconds >= 1.0);
        const QcHitEffect *teleport = m_content->projectile(u"qtcraft:ender_pearl"_s)->effect(QcHitEffect::teleport);
        QVERIFY(teleport && teleport->number == 5.0);
        QVERIFY(m_content->projectile(u"qtcraft:ender_pearl"_s)->breaks());

        // A gaze and carrying go with the neutral AI; carrying names its blocks.
        const auto parse = [](const char *json) {
            return QcContentParser::parseMob(QJsonDocument::fromJson(json).object(), u"qtcraft"_s).has_value();
        };
        QVERIFY(parse(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","hostile":true,"ai":"neutral","gaze":{},"carries":{"blocks":["dirt"]},"teleport":{}})"));
        QVERIFY(!parse(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","hostile":true,"gaze":{}})"));
        QVERIFY(!parse(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","hostile":true,"ai":"neutral","carries":{"blocks":[]}})"));
        QVERIFY(!parse(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","flying":true,"teleport":{}})"));

        // Unknown blocks are left out with a warning; with none left it carries nothing.
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcMobDefinition odd = endermanKind();
        odd.name = u"test:odd"_s;
        odd.carries->blocks = {u"test:nothing"_s};
        content->registerMob(odd);
        const QStringList warnings = content->finish();
        QVERIFY2(warnings.contains(u"mob 'test:odd': no block 'test:nothing' with an item to carry"_s), qPrintable(warnings.join(u'\n')));
        QVERIFY(!content->mobs()[*content->mobIndex(u"test:odd"_s)].carries);
    }

    void aLookAtTheHeadProvokes()
    {
        joinArena();
        const quint32 enderman = spawn("enderman", 0, -8, true);
        // Looking at its legs or past it leaves it calm.
        for (int tick = 0; tick < 3 * QcMobSteering::sightCheckTicks; ++tick) {
            lookAt(mob(enderman)->motion.position + QcVec3{0.0, 0.8, 0.0});
            ticks(1);
        }
        QCOMPARE(mob(enderman)->provokedBy, 0u);
        QVERIFY(!QcNeutralBrain::gazedAt({player().id, player().motion.position, true, true, QcProjectileFlight::direction(player().yaw, player().pitch)},
                                         headOf(enderman), *endermanKind().gaze, m_server->world()));

        // A look at its head makes it angry within a turn of its looking.
        for (int tick = 0; tick <= QcMobSteering::sightCheckTicks && mob(enderman)->provokedBy == 0; ++tick) {
            lookAt(headOf(enderman));
            ticks(1);
        }
        QCOMPARE(mob(enderman)->provokedBy, player().id);

        // Not through glass, and not from beyond the gaze's range.
        const QcMobManager::PlayerView view{player().id, player().motion.position, true, true, QcProjectileFlight::direction(player().yaw, player().pitch)};
        QVERIFY(QcNeutralBrain::gazedAt(view, headOf(enderman), *endermanKind().gaze, m_server->world()));
        const QcBlockPos between = QcPhysics::blockContaining((headOf(enderman) + view.position + QcVec3{0.0, 1.62, 0.0}) * 0.5);
        m_server->world().setBlock(between, id("qtcraft:glass"));
        QVERIFY(!QcNeutralBrain::gazedAt(view, headOf(enderman), *endermanKind().gaze, m_server->world()));
        QcGazeDefinition shortSight = *endermanKind().gaze;
        shortSight.range = 4.0;
        m_server->world().setBlock(between, qcAirBlockId);
        QVERIFY(!QcNeutralBrain::gazedAt(view, headOf(enderman), shortSight, m_server->world()));
    }

    void aProvokedEndermanAttacksAndAHurtOneTurns()
    {
        joinArena();
        const quint32 calm = spawn("enderman", 6, 0);
        ticks(40);
        QCOMPARE(player().health, QcServerClient::maxHealth);

        // Hurt by the player, it turns on them and strikes.
        m_server->entities().creatures().damage({.creature = calm, .damage = 1.0, .from = player().motion.position, .attacker = player().id});
        ticks(1);
        QCOMPARE(mob(calm)->provokedBy, player().id);
        for (int tick = 0; tick < 100 && player().health >= QcServerClient::maxHealth; ++tick) {
            ticks(1);
        }
        QVERIFY(player().health <= QcServerClient::maxHealth - endermanKind().attackDamage + 1e-9);
    }

    void teleportsOnlyToSafePlacesInLoadedChunks()
    {
        // Chunk (0, 0) is water three deep; chunk (1, 0) has a stone floor with lava pools and a
        // low roof; no other chunk is loaded.
        QcServerWorld world(m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        world.loadChunk({0, 0});
        world.loadChunk({1, 0});
        const int floor = 60;
        for (int x = 0; x < 32; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = floor - 6; y <= floor + 24; ++y) {
                    world.setBlock({x, y, z}, y <= floor - 1 ? id("qtcraft:stone") : qcAirBlockId);
                }
                if (x < 16) {
                    for (int y = floor; y < floor + 3; ++y) {
                        world.setBlock({x, y, z}, id("qtcraft:water"));
                    }
                } else if ((x + z) % 5 == 0) {
                    world.setBlock({x, floor, z}, id("qtcraft:lava"));
                } else if (x < 21) {
                    world.setBlock({x, floor + 2, z}, id("qtcraft:stone"));
                }
            }
        }

        const QcVec3 from{15.5, double(floor), 8.5};
        const int cells = QcSafeSpot::cellsOf(endermanKind().height);
        QCOMPARE(cells, 3);
        int found = 0;
        for (quint32 seed = 1; seed <= 300; ++seed) {
            QRandomGenerator random(seed);
            const std::optional<QcVec3> target = QcSafeSpot::randomAround(world, from, {8.0, 16, cells}, random);
            if (!target) {
                continue;
            }
            ++found;
            // Across the chunk border into the loaded chunk only, on its floor or on the low roof,
            // never into the lava, under the low roof or into the water.
            const QcBlockPos feet = QcPhysics::blockContaining(*target);
            const bool onFloor = feet.x >= 21 && feet.y == floor;
            const bool onRoof = feet.x >= 16 && feet.x < 21 && feet.y == floor + 3;
            QVERIFY2((onFloor || onRoof) && feet.x < 32 && feet.z >= 0 && feet.z < 16,
                     qPrintable(u"%1 %2 %3"_s.arg(feet.x).arg(feet.y).arg(feet.z)));
            QVERIFY(standsSafely(world, *target, cells));
        }
        QVERIFY(found > 100);

        // The nearest place for a player: above a point in the floor, none in the water or where
        // the chunk is not loaded.
        QCOMPARE(QcSafeSpot::nearest(world, {25.3, floor - 0.2, 6.6}, {0.0, -1.0, 0.0}, 2), std::optional(QcVec3{25.5, double(floor), 6.5}));
        QVERIFY(!QcSafeSpot::nearest(world, {8.5, floor + 0.5, 8.5}, {}, 2));
        QVERIFY(!QcSafeSpot::nearest(world, {40.5, floor + 0.5, 8.5}, {}, 2));

        // Without the dry chunk there is no safe place at all.
        QVERIFY(world.takeChunk({1, 0}));
        for (quint32 seed = 1; seed <= 50; ++seed) {
            QRandomGenerator random(seed);
            QVERIFY(!QcSafeSpot::randomAround(world, from, {8.0, 16, cells}, random));
        }
    }

    void projectilesMakeItTeleportUnhurt()
    {
        joinArena();
        const quint32 enderman = spawn("enderman", 4, -2);
        const QcVec3 before = mob(enderman)->motion.position;
        m_server->systems().projectiles.launch({0, u"qtcraft:arrow"_s, {m_feet.x - 1.5, m_feet.y + 1.0, m_feet.z - 1.5}, {40.0, 0.0, 0.0}});
        for (int tick = 0; tick < 20 && (mob(enderman)->motion.position - before).length() < 2.0; ++tick) {
            ticks(1);
        }
        QCOMPARE(mob(enderman)->health, endermanKind().maxHealth);
        QVERIFY((mob(enderman)->motion.position - before).length() >= 2.0);
        QVERIFY(standsSafely(m_server->world(), mob(enderman)->motion.position, 3));

        // A confined one stays and is not hurt either.
        const quint32 penned = spawn("enderman", 4, 6, true);
        const QcVec3 pen = mob(penned)->motion.position;
        m_server->systems().projectiles.launch({0, u"qtcraft:arrow"_s, {pen.x - 5.0, pen.y + 1.0, pen.z}, {40.0, 0.0, 0.0}});
        ticks(10);
        QCOMPARE(mob(penned)->health, endermanKind().maxHealth);
        QVERIFY((mob(penned)->motion.position - pen).length() < 1.0);
    }

    void waterAndRainHurtAndMakeItTeleport()
    {
        joinArena();
        // A confined one in water takes the damage and stays.
        const quint32 penned = spawn("enderman", -6, -6, true);
        m_server->world().setBlock(m_feet.offset(-6, 0, -6), id("qtcraft:water"));
        m_server->world().setBlock(m_feet.offset(-6, 1, -6), id("qtcraft:water"));
        ticks(30);
        QVERIFY(mob(penned)->health < endermanKind().maxHealth);

        // A free one leaves the water at once.
        const quint32 wet = spawn("enderman", 6, 6);
        m_server->world().setBlock(m_feet.offset(6, 0, 6), id("qtcraft:water"));
        m_server->world().setBlock(m_feet.offset(6, 1, 6), id("qtcraft:water"));
        ticks(3);
        QVERIFY(!QcPhysics::isLiquidAt(m_server->world(), QcPhysics::blockContaining(mob(wet)->motion.position + QcVec3{0.0, 0.4, 0.0})));
        QVERIFY(QcPhysics::blockContaining(mob(wet)->motion.position) != m_feet.offset(6, 0, 6));

        // In the rain it is hurt and jumps away; under a roof it stays dry.
        chat(u"/gamerule weatherCycle false"_s);
        chat(u"/weather rain"_s);
        const quint32 sheltered = spawn("enderman", -12, 12, true);
        for (int dx = -9; dx <= 9; ++dx) {
            for (int dz = -9; dz <= 9; ++dz) {
                m_server->world().setBlock(m_feet.offset(-12 + dx, 4, 12 + dz), id("qtcraft:stone"));
            }
        }
        const quint32 rained = spawn("enderman", 12, -12);
        const QcVec3 start = mob(rained)->motion.position;
        ticks(QcWeatherCycle::fadeTicks + 30);
        QVERIFY(mob(rained)->health < endermanKind().maxHealth);
        QVERIFY((mob(rained)->motion.position - start).length() > 2.0);
        QCOMPARE(mob(sheltered)->health, endermanKind().maxHealth);
    }

    void blocksAreTakenAndPlacedUnderTheGameRule()
    {
        setCarryChances(1.0, 0.0);
        joinArena();
        // With mobGriefing off nothing is taken.
        chat(u"/gamerule mobGriefing false"_s);
        const quint32 enderman = spawn("enderman", 5, 5);
        ticks(200);
        QCOMPARE(mob(enderman)->carried, qcAirBlockId);
        QVERIFY(!mob(enderman)->persistent);

        // On, it takes a grass block of the floor; the block is gone and it is kept with the world.
        chat(u"/gamerule mobGriefing true"_s);
        const int grassBefore = grassAt(-1);
        for (int tick = 0; tick < 400 && mob(enderman)->carried == qcAirBlockId; ++tick) {
            ticks(1);
        }
        QCOMPARE(mob(enderman)->carried, id("qtcraft:grass_block"));
        QCOMPARE(mob(enderman)->carriedItem, item("qtcraft:grass_block"));
        QVERIFY(mob(enderman)->persistent && mob(enderman)->uid != 0);
        QCOMPARE(grassAt(-1), grassBefore - 1);

        // Saved and loaded with its chunk, it still carries the block.
        const QcChunkPos chunk = QcEntities::chunkOf(*mob(enderman));
        const QHash<QcChunkPos, QcEntityRecord> records = QcEntityRecords::take(m_server->entities(), {chunk});
        QStringList warnings;
        QSet<QString> reported;
        const QcResult<QcEntityRecord> back = QcEntityCodec::decode(QcEntityCodec::encode(records[chunk], *m_content), chunk, *m_content, warnings, reported);
        QVERIFY(back.has_value());
        QVERIFY(QcEntityRecords::apply(*back, m_server->entities()).isEmpty());
        const auto loaded = std::ranges::find(m_server->entities().creatures().mobs(), type("enderman"), &QcCreature::type);
        QVERIFY(loaded != m_server->entities().creatures().mobs().end());
        QCOMPARE(loaded->carried, id("qtcraft:grass_block"));
        QCOMPARE(loaded->carriedItem, item("qtcraft:grass_block"));
        const quint32 carrier = loaded->id;

        // When it dies it drops the block.
        m_server->entities().creatures().damage({.creature = carrier, .damage = 1000.0, .from = player().motion.position});
        QVERIFY(!mob(carrier));
        QVERIFY(droppedCount(item("qtcraft:grass_block")) >= 1);
    }

    void aCarriedBlockIsPutDown()
    {
        setCarryChances(1.0, 1.0);
        joinArena();
        const int floorGrass = grassAt(-1);
        QCOMPARE(grassAt(0), 0);
        const quint32 enderman = spawn("enderman", 5, 5);
        for (int tick = 0; tick < 400 && mob(enderman)->carried == qcAirBlockId; ++tick) {
            ticks(1);
        }
        QCOMPARE(mob(enderman)->carried, id("qtcraft:grass_block"));

        // Put down into the air on a full block, never into a body; then it may go again.
        for (int tick = 0; tick < 400 && mob(enderman)->carried != qcAirBlockId; ++tick) {
            ticks(1);
        }
        QCOMPARE(mob(enderman)->carried, qcAirBlockId);
        QVERIFY(!mob(enderman)->persistent);
        // The block taken from the floor now stands on it, nothing lost, nothing gained.
        QCOMPARE(grassAt(-1), floorGrass - 1);
        QCOMPARE(grassAt(0), 1);
    }

    void enderPearlsTeleportTheThrower()
    {
        joinArena(noon);
        player().inventory.add(item("qtcraft:ender_pearl"), 2, *m_content);
        for (int slot = 0; slot < 9; ++slot) {
            if (player().inventory.slot(slot).item == item("qtcraft:ender_pearl")) {
                player().inventory.setSelectedHotbarSlot(slot);
            }
        }
        const QcVec3 start = player().motion.position;
        lookAt(start + QcVec3{0.0, 1.2, -10.0});
        send(QcMsg::ThrowItem{});
        QCOMPARE(player().inventory.countOf(item("qtcraft:ender_pearl")), 1);
        // The cooldown of a second keeps the next one in the hand.
        send(QcMsg::ThrowItem{});
        QCOMPARE(player().inventory.countOf(item("qtcraft:ender_pearl")), 1);

        for (int tick = 0; tick < 100 && !m_server->entities().projectiles().all().isEmpty(); ++tick) {
            ticks(1);
        }
        QVERIFY(m_server->entities().projectiles().all().isEmpty());
        QVERIFY((player().motion.position - start).length() > 4.0);
        QVERIFY(QcSafeSpot::fits(m_server->world(), QcPhysics::blockContaining(player().motion.position), 2));
        QCOMPARE(player().health, QcServerClient::maxHealth - 5.0);

        // On a creature it teleports too.
        player().health = QcServerClient::maxHealth;
        const QcBlockPos here = QcPhysics::blockContaining(player().motion.position);
        const quint32 pig = m_server->entities().creatures().spawn(type("pig"), {here.x + 0.5, double(here.y), here.z + 4.5},
                                                                   m_server->entities().newId());
        const QcVec3 pigAt = mob(pig)->motion.position;
        const QcVec3 eyes = player().motion.position + QcVec3{0.0, 1.0, 0.0};
        m_server->systems().projectiles.launch({player().id, u"qtcraft:ender_pearl"_s, eyes, (pigAt + QcVec3{0.0, 0.5, 0.0} - eyes).normalized() * 20.0});
        for (int tick = 0; tick < 40 && !m_server->entities().projectiles().all().isEmpty(); ++tick) {
            ticks(1);
        }
        QVERIFY((player().motion.position - pigAt).length() < 2.0);
    }

    void spawnsRarelyOnDarkNights()
    {
        // Only endermen spawn as hostile creatures here, so the cap does not fill with others.
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        for (const char *other : {"zombie", "skeleton", "slime"}) {
            QcMobDefinition quiet = m_content->mobs()[type(other)];
            quiet.spawnWeight = 0.0;
            quiet.extraSpawns.clear();
            content->registerMob(quiet);
        }
        m_content = content;
        QCOMPARE(endermanKind().maxGroup, 1);
        QVERIFY(spawned("enderman", noon, 60).isEmpty());
        QVERIFY(!spawned("enderman", midnight, 120).isEmpty());
        QVERIFY(!m_spawnedInsideBlock);
    }

    // 30 endermen at night around a player who looks at one of them, taking and placing blocks
    // (mobGriefing on, ten times the usual chances), with ticks stepped as fast as the server can
    // (decision E-460). QTCRAFT_ENDERMAN_MEASURE=full runs 400 ticks and prints the times; otherwise
    // 40 ticks check that it works.
    void thirtyEndermenMeasure()
    {
        const bool full = qEnvironmentVariable("QTCRAFT_ENDERMAN_MEASURE") == u"full"_s;
        const int count = full ? 400 : 40;
        setCarryChances(0.5, 0.05);
        joinArena();
        QList<quint32> endermen;
        for (int i = 0; i < 30; ++i) {
            const double angle = 2.0 * std::numbers::pi * i / 30.0;
            const double radius = 6.0 + 2.0 * (i % 4);
            endermen.append(spawn("enderman", int(std::lround(radius * std::cos(angle))), int(std::lround(radius * std::sin(angle)))));
        }

        double tickMs = 0.0;
        double mobsMs = 0.0;
        double maxTickMs = 0.0;
        for (int i = 0; i < count; ++i) {
            player().health = QcServerClient::maxHealth;
            if (mob(endermen.front())) {
                lookAt(headOf(endermen.front()));
            }
            QElapsedTimer timer;
            timer.start();
            m_server->tick();
            const double ms = double(timer.nsecsElapsed()) / 1e6;
            tickMs += ms / count;
            maxTickMs = std::max(maxTickMs, ms);
            mobsMs += double(m_server->tickPhases().lastTick()[std::size_t(QcTickPhase::Mobs)]) / 1e6 / count;
            pump();
        }
        int carrying = 0;
        int angry = 0;
        for (const QcCreature &creature : m_server->entities().creatures().mobs()) {
            carrying += creature.carried != qcAirBlockId ? 1 : 0;
            angry += creature.provokedBy != 0 ? 1 : 0;
        }
        QVERIFY(angry >= 1);
        qInfo("MEASURE endermen=30 ticks=%d carrying=%d angry=%d tick_mean_ms=%.4f tick_max_ms=%.4f mobs_mean_ms=%.4f", count, carrying,
              angry, tickMs, maxTickMs, mobsMs);
    }
};

QTEST_GUILESS_MAIN(TestEndermen)
#include "test_endermen.moc"
