// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/mobs/QcMobSerializer.h"
#include "common/physics/QcBodyFit.h"
#include "mobs/QcMobStageFixture.h"
#include "server/mobbrains/QcMobSteering.h"
#include "server/projectiles/QcProjectileFlight.h"
#include "server/projectiles/QcProjectileManager.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// The creatures of the nether (decisions E-1052 to E-1055): the Rottusk's pack, the magma slime that
// hops and falls apart, the wailer's fire that a strike sends back, and the lava wader that walks on
// lava and carries a saddle and a rider. Ticks are stepped by hand.
class TestNetherMobs : public QcMobStageFixture
{
    Q_OBJECT

private:
    void input(float yaw, float pitch, bool sneak = false)
    {
        QcPlayerInput wish;
        wish.sequence = m_sequence++;
        wish.yaw = yaw;
        wish.pitch = pitch;
        wish.sneak = sneak;
        send(QcMsg::PlayerInput{wish});
    }

    // A wader one block east of the player, saddled by him (it spawned as any wild one does).
    quint32 saddledWader()
    {
        const quint32 wader = spawn(u"lava_wader"_s, 1, 0);
        hold("qtcraft:saddle");
        send(QcMsg::UseEntity{wader});
        ticks(1);
        return wader;
    }

    // The same with the player in the saddle.
    quint32 ridden()
    {
        const quint32 wader = saddledWader();
        send(QcMsg::UseEntity{wader});
        ticks(1);
        return wader;
    }

    // A fireball of `shooter` straight down at the stage, two blocks east of the player, with a pig
    // beside the place. Returns whether it really burst: gone within a second and the pig hurt.
    bool fireballBurstsOnTheStage(quint32 shooter)
    {
        const quint32 pig = spawn(u"pig"_s, 3, 0);
        m_server->mobs().find(pig)->noAi = true;
        const QcVec3 above{m_feet.x + 2.5, m_feet.y + 6.0, m_feet.z + 0.5};
        const quint32 fireball = m_server->entities().newId();
        if (!m_server->entities().projectiles().launch({shooter, u"qtcraft:fireball"_s, above, {0.0, -16.0, 0.0}, QcProjectilePickup::Never, 1.0}, fireball)) {
            return false;
        }
        for (int i = 0; i < 20 && m_server->entities().projectiles().find(fireball); ++i) {
            ticks(1);
        }
        const QcCreature *hurt = m_server->mobs().find(pig);
        return !m_server->entities().projectiles().find(fireball) && (!hurt || hurt->health < m_content->mobs()[hurt->type].maxHealth);
    }

    // How many ground blocks around the place of fireballBurstsOnTheStage() are gone.
    int craterSize() const
    {
        int gone = 0;
        for (int dx = 0; dx <= 3; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                gone += m_server->world().blockAt(m_feet.offset(dx, -1, dz)) != id("qtcraft:grass_block") ? 1 : 0;
            }
        }
        return gone;
    }

    quint32 m_sequence = 100000;

private slots:
    void aRottuskLeavesPlayersAloneUntilStruck()
    {
        joinStage();
        const quint32 near = spawn(u"rottusk"_s, 2, 0);
        ticks(40);
        QCOMPARE(m_server->mobs().find(near)->chasing, 0u);
    }

    void aBlowAngersThePackInRangeOnly()
    {
        joinStage();
        const quint32 struck = spawn(u"rottusk"_s, 1, 1);
        const quint32 friendNear = spawn(u"rottusk"_s, -2, 1);
        const QcVec3 far{m_feet.x + 30.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        loadAround(m_feet.offset(30, 0, 0), 1);
        const quint32 friendFar = m_server->mobs().spawn(kind(u"rottusk"_s), far, m_server->entities().newId());
        send(QcMsg::AttackEntity{struck});
        ticks(5);
        QCOMPARE(m_server->mobs().find(struck)->provokedBy, player().id);
        QCOMPARE(m_server->mobs().find(friendNear)->provokedBy, player().id);
        QCOMPARE(m_server->mobs().find(friendFar)->provokedBy, 0u);
    }

    void fireImmuneCreaturesStandInLava()
    {
        joinStage();
        floorOf("qtcraft:lava", 1);
        const quint32 tusk = spawn(u"rottusk"_s, 0, 0, -1.0);
        const quint32 slime = spawn(u"magma_slime"_s, 1, 0, -1.0);
        const quint32 zombie = spawn(u"zombie"_s, -1, 0, -1.0);
        ticks(100);
        QVERIFY(m_server->mobs().find(tusk));
        QCOMPARE(m_server->mobs().find(tusk)->health, m_content->mobs()[kind(u"rottusk"_s)].maxHealth);
        QCOMPARE(m_server->mobs().find(tusk)->burnTicks, 0);
        QCOMPARE(m_server->mobs().find(slime)->health, m_content->mobs()[kind(u"magma_slime"_s)].maxHealth);
        const QcCreature *burnt = m_server->mobs().find(zombie);
        QVERIFY(!burnt || burnt->health < m_content->mobs()[kind(u"zombie"_s)].maxHealth);
    }

    void aMagmaSlimeHopsAndFallsApart()
    {
        joinStage();
        const quint32 slime = spawn(u"magma_slime"_s, 2, 0);
        bool airborne = false;
        for (int i = 0; i < 40 && m_server->mobs().find(slime); ++i) {
            ticks(1);
            airborne = airborne || !m_server->mobs().find(slime)->motion.onGround;
        }
        QVERIFY(airborne);
        QVERIFY(m_server->mobs().hurt(slime, 1000.0));
        ticks(3);
        qsizetype pieces = 0;
        for (const QcCreature &mob : m_server->mobs().mobs()) {
            pieces += mob.type == kind(u"magma_slime_small"_s);
        }
        QVERIFY2(pieces >= 2 && pieces <= 4, qPrintable(QString::number(pieces)));
    }

    void aSmallPieceLeavesNoPieces()
    {
        joinStage();
        const quint32 piece = spawn(u"magma_slime_small"_s, 2, 0);
        QVERIFY(m_server->mobs().hurt(piece, 1000.0));
        ticks(3);
        QVERIFY(m_server->mobs().mobs().isEmpty());
    }

    void aWailerShootsWhatItSees()
    {
        joinStage();
        const quint32 wailer = spawn(u"wailer"_s, 0, 12, 4.0);
        const QcProjectileManager &projectiles = m_server->entities().projectiles();
        bool shot = false;
        for (int i = 0; i < 80 && !shot; ++i) {
            ticks(1);
            shot = !projectiles.all().isEmpty();
        }
        QVERIFY(shot);
        QCOMPARE(projectiles.all().constFirst().shooter, wailer);
        QCOMPARE(projectiles.all().constFirst().type, u"qtcraft:fireball"_s);
    }

    // The wailer cries out half a second (10 ticks, as in the role model) before the fireball leaves.
    void aWailerWarnsBeforeItShoots()
    {
        joinStage();
        spawn(u"wailer"_s, 0, 12, 4.0);
        const QString warning = m_content->mobs()[kind(u"wailer"_s)].archer.warnSound;
        QVERIFY(!warning.isEmpty());
        int warnedAt = -1;
        int shotAt = -1;
        for (int i = 0; i < 100 && shotAt < 0; ++i) {
            ticks(1);
            QcTestUtil::pump();
            if (warnedAt < 0 && m_client->sounds.contains(warning)) {
                warnedAt = i;
            }
            shotAt = m_server->entities().projectiles().all().isEmpty() ? -1 : i;
        }
        QVERIFY(shotAt >= 0);
        QVERIFY(warnedAt >= 0);
        QVERIFY2(shotAt - warnedAt >= 8 && shotAt - warnedAt <= 12, qPrintable(u"warned %1, shot %2"_s.arg(warnedAt).arg(shotAt)));
        QCOMPARE(m_client->sounds.count(warning), 1);
    }

    // A wailer drifts slowly enough to be followed and hit (the human, 2026-11-06: about 12
    // blocks in 4 seconds were too fast, most of it rising to its height): its fastest speed over
    // 4 seconds, sideways and with the climb.
    void aWailerDriftsSlowly()
    {
        joinStage();
        const quint32 wailer = spawn(u"wailer"_s, 0, 0, 4.0);
        ticks(20);
        double fastest3d = 0.0;
        double fastest = 0.0;
        double travelled = 0.0;
        QcVec3 last = m_server->mobs().find(wailer)->motion.position;
        for (int i = 0; i < 4 * QcPhysics::tickRate; ++i) {
            ticks(1);
            const QcVec3 now = m_server->mobs().find(wailer)->motion.position;
            const double step = QcVec3{now.x - last.x, 0.0, now.z - last.z}.length();
            fastest = std::max(fastest, step * QcPhysics::tickRate);
            fastest3d = std::max(fastest3d, (now - last).length() * QcPhysics::tickRate);
            travelled += step;
            last = now;
        }
        qInfo() << "wailer: fastest" << fastest << "m/s (with height" << fastest3d << "), travelled" << travelled << "m in 4 s";
        QVERIFY(travelled > 0.5);
        QVERIFY2(fastest <= 0.9, qPrintable(QString::number(fastest)));
        QVERIFY2(fastest3d <= 1.4, qPrintable(QString::number(fastest3d)));
    }

    void aFireballBurstsWithoutBreakingBlocksUnderTheRule()
    {
        joinStage();
        chat(u"/gamerule mobGriefing false"_s);
        const quint32 wailer = spawn(u"wailer"_s, 0, 12, 4.0);
        const QcBlockPos stage = m_feet.offset(0, -1, 0);
        const QcProjectileManager &projectiles = m_server->entities().projectiles();
        bool burst = false;
        std::optional<QcVec3> last;
        for (int i = 0; i < 200 && !burst; ++i) {
            ticks(1);
            burst = last && projectiles.all().isEmpty();
            if (!projectiles.all().isEmpty()) {
                last = projectiles.all().constFirst().motion.position;
            }
        }
        QVERIFY(burst);
        // It ended at the player or the ground it was aimed at, not somewhere in the air.
        QVERIFY2((*last - player().motion.position).length() < 6.0, qPrintable(QString::number((*last - player().motion.position).length())));
        QVERIFY(m_server->mobs().find(wailer));
        QCOMPARE(*m_server->world().blockAt(stage), id("qtcraft:grass_block"));
    }

    void aFireballBreaksTheGroundWhereTheRuleAllows()
    {
        joinStage();
        const quint32 wailer = spawn(u"wailer"_s, 0, 12, 4.0);
        m_server->mobs().find(wailer)->noAi = true;
        QVERIFY(fireballBurstsOnTheStage(wailer));
        QVERIFY(craterSize() > 0);
    }

    void aFireballWhoseShooterIsGoneKeepsToTheRule()
    {
        joinStage();
        chat(u"/gamerule mobGriefing false"_s);
        QVERIFY(fireballBurstsOnTheStage(0));
        QCOMPARE(craterSize(), 0);
    }

    void aFireballStruckBackIsThePlayersAndBreaksTheGround()
    {
        joinStage();
        chat(u"/gamerule mobGriefing false"_s);
        QVERIFY(fireballBurstsOnTheStage(player().id));
        QVERIFY(craterSize() > 0);
    }

    void aStruckFireballFliesBackAndBringsTheWailerDown()
    {
        joinStage(u"creative"_s);
        chat(u"/gamemode survival"_s);
        const quint32 wailer = spawn(u"wailer"_s, 0, 8, 2.0);
        QcCreature &body = *m_server->mobs().find(wailer);
        body.noAi = true;
        // A fireball of the wailer's right before the player.
        const QcVec3 eyes = player().motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
        const QcVec3 towards = (body.motion.position + QcVec3{0.0, 1.6, 0.0} - eyes).normalized();
        const quint32 fireball = m_server->entities().newId();
        QVERIFY(m_server->entities().projectiles().launch({wailer, u"qtcraft:fireball"_s, eyes + towards * 2.5, towards * -16.0,
                                                           QcProjectilePickup::Never, 1.0}, fireball));
        // The strike aims where the player looks: at the wailer.
        player().yaw = QcMobSteering::yawTowards(towards);
        player().pitch = static_cast<float>(qRadiansToDegrees(std::asin(towards.y)));
        send(QcMsg::AttackEntity{fireball});
        ticks(1);
        const QcProjectile *struck = m_server->entities().projectiles().find(fireball);
        QVERIFY(struck);
        QCOMPARE(struck->shooter, player().id);
        QVERIFY(struck->motion.velocity.dot(towards) > 10.0);
        for (int i = 0; i < 60 && m_server->mobs().find(wailer); ++i) {
            ticks(1);
        }
        QVERIFY(!m_server->mobs().find(wailer));
    }

    void anArrowCannotBeStruck()
    {
        joinStage();
        const quint32 arrow = m_server->entities().newId();
        const QcVec3 at = player().motion.position + QcVec3{1.0, 1.5, 0.0};
        QVERIFY(m_server->entities().projectiles().launch({0, u"qtcraft:arrow"_s, at, {5.0, 0.0, 0.0}, QcProjectilePickup::Never, 1.0}, arrow));
        send(QcMsg::AttackEntity{arrow});
        QCOMPARE(m_server->entities().projectiles().find(arrow)->shooter, 0u);
    }

    void aWaderWalksOnLavaUnhurt()
    {
        joinStage();
        floorOf("qtcraft:lava", 3);
        const quint32 wader = spawn(u"lava_wader"_s, 1, 0);
        const double surface = m_feet.y;
        double travelled = 0.0;
        QcVec3 last = m_server->mobs().find(wader)->motion.position;
        for (int i = 0; i < 300; ++i) {
            ticks(1);
            const QcCreature &mob = *m_server->mobs().find(wader);
            travelled += (mob.motion.position - last).length();
            last = mob.motion.position;
            QVERIFY2(std::abs(mob.motion.position.y - surface) < 0.2, qPrintable(QString::number(mob.motion.position.y)));
        }
        QVERIFY(travelled > 1.0);
        QCOMPARE(m_server->mobs().find(wader)->health, m_content->mobs()[kind(u"lava_wader"_s)].maxHealth);
    }

    void waterHurtsAWader()
    {
        joinStage();
        const quint32 wader = spawn(u"lava_wader"_s, 2, 0);
        m_server->world().setBlock(m_feet.offset(2, 0, 0), id("qtcraft:water"));
        m_server->world().setBlock(m_feet.offset(2, 1, 0), id("qtcraft:water"));
        ticks(60);
        const QcCreature *mob = m_server->mobs().find(wader);
        QVERIFY(!mob || mob->health < m_content->mobs()[kind(u"lava_wader"_s)].maxHealth);
    }

    void aSaddledWaderCarriesAndFollowsItsRider()
    {
        joinStage();
        floorOf("qtcraft:lava", 6);
        player().motion.position = QcVec3{m_feet.x + 0.5, m_feet.y + 0.2, m_feet.z + 0.5};
        const quint32 wader = spawn(u"lava_wader"_s, 1, 0);
        m_server->mobs().find(wader)->noAi = false;

        // Bare hands neither saddle nor mount it.
        send(QcMsg::UseEntity{wader});
        QVERIFY(!m_server->mobs().find(wader)->saddled);
        QCOMPARE(m_server->mobs().find(wader)->rider, 0u);

        hold("qtcraft:saddle");
        send(QcMsg::UseEntity{wader});
        QVERIFY(m_server->mobs().find(wader)->saddled);
        QCOMPARE(countOf(item("qtcraft:saddle")), 0);

        send(QcMsg::UseEntity{wader});
        ticks(1);
        QCOMPARE(m_server->mobs().find(wader)->rider, player().id);
        const double seat = m_content->mobs()[kind(u"lava_wader"_s)].ride->seat;
        QCOMPARE(player().motion.position.y, m_server->mobs().find(wader)->motion.position.y + seat);

        // With the fungus on a stick it goes where the rider looks (yaw 90: west, as QcProjectileFlight).
        hold("qtcraft:fungus_on_a_stick");
        const QcVec3 start = m_server->mobs().find(wader)->motion.position;
        for (int i = 0; i < 40; ++i) {
            input(90.0f, 0.0f);
            ticks(1);
        }
        const QcVec3 gone = m_server->mobs().find(wader)->motion.position - start;
        const QcVec3 look = QcProjectileFlight::direction(90.0f, 0.0f);
        QVERIFY2(gone.dot(look) > 3.0, qPrintable(QString::number(gone.dot(look))));

        input(90.0f, 0.0f, true);
        ticks(2);
        QCOMPARE(m_server->mobs().find(wader)->rider, 0u);
    }

    // A saddled creature leaves its saddle when it dies, an unsaddled one none.
    void aSaddleFallsWhenTheWaderDies()
    {
        joinStage();
        const QString saddle = m_content->mobs()[kind(u"lava_wader"_s)].ride->saddle;
        const auto saddles = [&](const std::optional<QList<QcItemDrop>> &drops) {
            return drops ? static_cast<int>(std::ranges::count(*drops, saddle, &QcItemDrop::item)) : -1;
        };
        const quint32 wild = spawn(u"lava_wader"_s, -1, 0);
        QCOMPARE(saddles(m_server->mobs().wound(wild, 1000.0)), 0);
        const quint32 saddled = saddledWader();
        QVERIFY(m_server->mobs().find(saddled)->saddled);
        QCOMPARE(saddles(m_server->mobs().wound(saddled, 1000.0)), 1);
    }

    void aSaddleStaysWithTheWorld()
    {
        QTemporaryDir dir;
        join(configIn(dir.path()));
        loadAround(m_feet, 2);
        m_server->mobs().setNaturalSpawning(false);
        m_server->mobs().removeAll();
        const quint32 wader = saddledWader();
        QVERIFY(m_server->mobs().find(wader)->saddled);
        QVERIFY(m_server->mobs().find(wader)->persistent);
        QVERIFY(m_server->save().has_value());

        reopen(dir.path());
        loadAround(m_feet, 2);
        const QList<QcCreature> &mobs = m_server->mobs().mobs();
        const auto found = std::ranges::find(mobs, kind(u"lava_wader"_s), &QcCreature::type);
        QVERIFY(found != mobs.end());
        QVERIFY(found->saddled);
    }

    void aRiderComesBackOnFirmGround_data()
    {
        QTest::addColumn<bool>("logsOut");
        QTest::newRow("autosave in the saddle") << false;
        QTest::newRow("logout in the saddle") << true;
    }

    void aRiderComesBackOnFirmGround()
    {
        QFETCH(bool, logsOut);
        QTemporaryDir dir;
        join(configIn(dir.path()));
        loadAround(m_feet, 2);
        m_server->mobs().setNaturalSpawning(false);
        m_server->mobs().removeAll();
        chat(u"/gamemode creative"_s);
        floorOf("qtcraft:lava", 3);
        const quint32 wader = ridden();
        QCOMPARE(m_server->mobs().find(wader)->rider, player().id);
        if (logsOut) {
            m_client.reset();
            QcTestUtil::pump();
            ticks(3);
            QCOMPARE(m_server->mobs().find(wader)->rider, 0u);
        }
        QVERIFY(m_server->save().has_value());

        reopen(dir.path());
        const QcVec3 feet = player().motion.position;
        QVERIFY(m_server->world().loadChunk(QcPhysics::blockContaining(feet).chunk()) != nullptr);
        const QcBlockDefinition &below = m_content->block(*m_server->world().blockAt(QcPhysics::blockContaining(feet).offset(0, -1, 0)));
        QVERIFY2(below.solid && !below.isLava(), qPrintable(below.name));
        QCOMPARE(m_client->vehicle, 0u);
    }

    void aRiderLosesHisMount()
    {
        joinStage();
        ridden();
        QVERIFY(m_client->vehicle != 0);
        m_server->mobs().removeAll();
        ticks(2);
        QVERIFY(m_server->mobs().riders().isEmpty());
        QCOMPARE(m_client->vehicle, 0u);
    }

    void aTeleportTakesTheRiderOff()
    {
        joinStage();
        const quint32 wader = ridden();
        const QcVec3 target{m_feet.x - 2.5, static_cast<double>(m_feet.y), m_feet.z - 2.5};
        chat(u"/tp %1 %2 %3"_s.arg(target.x).arg(target.y).arg(target.z));
        ticks(5);
        QCOMPARE(m_server->mobs().find(wader)->rider, 0u);
        QVERIFY((player().motion.position - target).length() < 0.6);
        QCOMPARE(m_client->vehicle, 0u);
    }

    void aRiderGetsOffAwayFromTheWall()
    {
        joinStage();
        const quint32 wader = ridden();
        m_server->mobs().find(wader)->noAi = true;
        const QcBlockPos at = QcPhysics::blockContaining(m_server->mobs().find(wader)->motion.position);
        // Walls east and west of it, and the block north; open only to the south.
        for (int dy = 0; dy <= 3; ++dy) {
            for (const auto [dx, dz] : {std::pair{1, -1}, {1, 0}, {1, 1}, {-1, -1}, {-1, 0}, {-1, 1}, {0, -1}}) {
                m_server->world().setBlock(at.offset(dx, dy, dz), id("qtcraft:stone"));
            }
        }
        input(0.0f, 0.0f, true);
        ticks(2);
        QCOMPARE(m_server->mobs().find(wader)->rider, 0u);
        const QcVec3 feet = player().motion.position;
        QVERIFY(!QcBodyFit::overlapsBlocks(m_server->world(), feet, QcPhysics::BodySize{}, QcBodyFit::Unloaded::Blocked));
        QCOMPARE(QcPhysics::blockContaining(feet).x, at.x);
    }

    void aWaderSinksInWater()
    {
        joinStage();
        for (int dx = -1; dx <= 3; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -3, dz), id("qtcraft:stone"));
                m_server->world().setBlock(m_feet.offset(dx, -2, dz), id("qtcraft:water"));
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:water"));
            }
        }
        const quint32 wader = spawn(u"lava_wader"_s, 1, 0);
        ticks(40);
        const QcCreature *mob = m_server->mobs().find(wader);
        QVERIFY(!mob || mob->motion.position.y < m_feet.y - 0.5);
    }

    void theSaddledPartsAndTheSeatReachTheClient()
    {
        const QcMobDefinition &wader = m_content->mobs()[kind(u"lava_wader"_s)];
        QVERIFY(std::ranges::any_of(wader.parts, &QcMobPart::saddled));
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        QcMobSerializer::writeMob(out, wader);
        QDataStream in(bytes);
        const std::optional<QcMobDefinition> read = QcMobSerializer::readMob(in);
        QVERIFY(read);
        QVERIFY(std::ranges::any_of(read->parts, &QcMobPart::saddled));
        QVERIFY(read->ride);
        QCOMPARE(read->ride->seat, wader.ride->seat);
    }
};

QTEST_GUILESS_MAIN(TestNetherMobs)
#include "test_nether_mobs.moc"
