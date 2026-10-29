// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gameplay/QcPetFixture.h"
#include "QcTestUtil.h"
#include "server/mobbrains/QcMobFear.h"
#include "server/projectiles/QcServerProjectiles.h"

#include <QElapsedTimer>

#include <functional>

using namespace Qt::StringLiterals;

// What pets and wild companions do (decision E-480): a dog fights what hurts its owner and what its
// owner strikes (blows and arrows), keeps one target until its owner fights anew, never its owner or
// a pet (a spider or a golem that hurt the owner too, while golems never hunt pets); players never
// hurt the pets of others; a struck wolf angers its pack at the attacker only; a shy ocelot runs
// from players unless they hold its fish and move slowly; and a measurement of 20 dogs following a
// moving player. Ticks are stepped by hand.
class TestPetRules : public QcPetFixture
{
    Q_OBJECT

private:
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

    // Ticks until the creature lost health or died; false on time-out.
    bool hurtWithin(quint32 creature, double health, int maxTicks)
    {
        for (int tick = 0; tick < maxTicks; ++tick) {
            ticks(1);
            if (!mob(creature) || mob(creature)->health < health) {
                return true;
            }
        }
        return false;
    }

    // Ticks until `done` holds; false when it does not within maxTicks.
    bool ticksUntil(const std::function<bool()> &done, int maxTicks)
    {
        for (int tick = 0; tick < maxTicks; ++tick) {
            ticks(1);
            if (done()) {
                return true;
            }
        }
        return false;
    }

    // The player strikes the creature through the game, from 2 m in front of it.
    void strike(quint32 creature)
    {
        player().motion.position = mob(creature)->motion.position + QcVec3{0.0, 0.0, 2.0};
        player().yaw = 0.0f;
        player().pitch = 20.0f;
        player().attackCooldown = 0.0;
        send(QcMsg::AttackEntity{creature});
        ticks(1);
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
        m_content = QcTestUtil::loadContent();
    }

    void aDogFightsWhatHurtsItsOwner()
    {
        joinArena(midnight);
        const quint32 dog = pet("dog", -3, 0);
        const quint32 zombie = spawn("zombie", 3, 0);
        player().motion.position = at(0, 0);
        const double full = mob(zombie)->health;
        // The zombie strikes the player; the dog takes it up.
        QVERIFY(hurtWithin(zombie, full, 200));
        QCOMPARE(player().foes.hurtBy, zombie);
        QVERIFY(mob(dog) && mob(dog)->tame->target == zombie);
    }

    void aDogFightsASpiderThatHurtItsOwner()
    {
        joinArena(midnight);
        const quint32 dog = pet("dog", -3, 0);
        const quint32 spider = spawn("spider", 3, 0);
        player().motion.position = at(0, 0);
        const double full = mob(spider)->health;
        // The spider strikes the player at night; the dog takes it up.
        QVERIFY(hurtWithin(spider, full, 200));
        QCOMPARE(player().foes.hurtBy, spider);
        QVERIFY(mob(dog) && mob(dog)->tame->target == spider);
    }

    // A golem the owner angered strikes them: the dog turns on it, and the golem, which hunts only
    // hostile creatures, leaves the dog alone.
    void aDogDefendsItsOwnerFromAGolemThatSparesIt()
    {
        joinArena();
        const quint32 dog = pet("dog", -4, 0);
        const quint32 golem = spawn("iron_golem", 4, 0);
        strike(golem);
        QVERIFY(ticksUntil([&] { return player().foes.hurtBy == golem; }, 400));
        const double dogHealth = mob(dog)->health;
        const double golemHealth = mob(golem)->health;
        QVERIFY(mob(dog)->tame->target == golem);
        QVERIFY(hurtWithin(golem, golemHealth, 200));
        QCOMPARE(mob(dog)->health, dogHealth);
    }

    void aDogAttacksWhatItsOwnerStrikesButNeverItsOwnersPets()
    {
        joinArena();
        const quint32 dog = pet("dog", -4, 0);
        const quint32 pig = spawn("pig", 4, 0);
        strike(pig);
        const double struck = mob(pig)->health;
        QCOMPARE(player().foes.struck, pig);
        QVERIFY(hurtWithin(pig, struck, 200));
        QCOMPARE(mob(dog)->tame->target, pig);

        // The owner strikes their own cat: the dog leaves it alone, and the owner too.
        m_server->mobs().remove(pig);
        const quint32 cat = pet("cat", 4, 4);
        const double ownerHealth = player().health;
        strike(cat);
        const double catHealth = mob(cat)->health;
        ticks(100);
        QCOMPARE(mob(dog)->tame->target, 0u);
        QCOMPARE(mob(cat)->health, catHealth);
        QCOMPARE(player().health, ownerHealth);
    }

    void aDogKeepsOneOfTwoFreshFoes()
    {
        // The zombie hurt the owner in the moment the owner struck the skeleton: the dog takes up one
        // and keeps it until it reaches it (creative mode: the monsters leave the owner alone).
        joinArena(midnight);
        player().gameMode = QcGameMode::Creative;
        const quint32 dog = pet("dog", 0, -4);
        const quint32 zombie = spawn("zombie", 6, 3);
        const quint32 skeleton = spawn("skeleton", -6, 3);
        strike(skeleton);
        player().foes.hurtBy = zombie;
        player().foes.hurtTick = player().foes.struckTick;
        player().motion.position = at(0, -6);
        const double zombieHealth = mob(zombie)->health;
        const double skeletonHealth = mob(skeleton)->health;

        quint32 chosen = 0;
        int switches = 0;
        bool reached = false;
        for (int tick = 0; tick < 300 && !reached; ++tick) {
            ticks(1);
            const quint32 target = mob(dog)->tame->target;
            switches += chosen != 0 && target != 0 && target != chosen ? 1 : 0;
            chosen = target != 0 ? target : chosen;
            reached = !mob(zombie) || mob(zombie)->health < zombieHealth || !mob(skeleton) || mob(skeleton)->health < skeletonHealth;
        }
        QCOMPARE(switches, 0);
        QVERIFY(chosen == zombie || chosen == skeleton);
        QVERIFY(reached);
    }

    void aNewerFightTurnsTheDog()
    {
        joinArena();
        player().gameMode = QcGameMode::Creative;
        const quint32 dog = pet("dog", 0, -4);
        const quint32 first = spawn("pig", 10, 0);
        const quint32 second = spawn("pig", -10, 0);
        strike(first);
        QVERIFY(ticksUntil([&] { return mob(dog)->tame->target == first; }, 20));
        strike(second);
        QVERIFY(ticksUntil([&] { return mob(dog)->tame->target == second; }, 20));
    }

    void othersPetsAreSpared()
    {
        joinArena();
        const std::unique_ptr<RawClient> other = joinOther();
        QcServerClient &stranger = *m_server->entities().clients().back();
        stranger.motion.position = at(-10, -10);
        const quint32 theirs = spawn("dog", 4, 0);
        QcPetCare::tame(m_server->entities(), *m_content, *mob(theirs), QcPetOwners::keyOf(stranger));
        const quint32 mine = pet("dog", -4, 0);
        const double full = mob(theirs)->health;

        // Neither a blow nor an arrow of the player hurts it, and it is no foe for the player's dog.
        strike(theirs);
        QCOMPARE(mob(theirs)->health, full);
        QVERIFY(player().foes.struck != theirs);
        const QcVec3 from = mob(theirs)->motion.position + QcVec3{-3.0, 0.5, 0.0};
        m_server->systems().projectiles.launch({player().id, u"qtcraft:arrow"_s, from, {30.0, 0.0, 0.0}});
        ticks(10);
        QCOMPARE(mob(theirs)->health, full);
        QVERIFY(player().foes.struck != theirs);
        player().foes.struck = theirs;
        player().foes.struckTick = m_server->tickCount();
        ticks(40);
        QCOMPARE(mob(mine)->tame->target, 0u);
        QCOMPARE(mob(theirs)->health, full);

        // Its owner's blow does hurt it (positive control).
        stranger.motion.position = mob(theirs)->motion.position + QcVec3{0.0, 0.0, 2.0};
        stranger.yaw = 0.0f;
        stranger.pitch = 20.0f;
        other->connection.send(QcMsg::AttackEntity{theirs});
        QcTestUtil::pump();
        ticks(1);
        QVERIFY(mob(theirs)->health < full);
    }

    void arrowsNameTheFoes()
    {
        joinArena();
        player().motion.position = at(0, 0);
        // The owner's arrow at a pig makes it the struck foe.
        const quint32 pig = spawn("pig", 0, 6);
        const QcVec3 aim = mob(pig)->motion.position + QcVec3{-3.0, 0.5, 0.0};
        m_server->systems().projectiles.launch({player().id, u"qtcraft:arrow"_s, aim, {30.0, 0.0, 0.0}});
        QVERIFY(ticksUntil([&] { return player().foes.struck == pig; }, 40));

        // A skeleton's arrow at the owner makes the skeleton the foe that hurt them.
        const quint32 skeleton = spawn("skeleton", -8, -8);
        const QcVec3 shot = player().motion.position + QcVec3{-3.0, 1.0, 0.0};
        m_server->systems().projectiles.launch({skeleton, u"qtcraft:arrow"_s, shot, {30.0, 0.0, 0.0}});
        QVERIFY(ticksUntil([&] { return player().foes.hurtBy == skeleton; }, 40));
    }

    void aDogNeverTurnsOnItsOwner()
    {
        joinArena();
        const quint32 dog = pet("dog", 0, 3);
        const double ownerHealth = player().health;
        strike(dog);
        QVERIFY(mob(dog)->health < kind("dog").maxHealth);
        ticks(100);
        QCOMPARE(mob(dog)->provokedBy, 0u);
        QCOMPARE(player().health, ownerHealth);
    }

    void aStruckWolfAngersItsPackAtTheAttackerOnly()
    {
        joinArena();
        const std::unique_ptr<RawClient> other = joinOther();
        QcServerClient &bystander = *m_server->entities().clients().back();
        bystander.motion.position = at(-6, -6);
        const quint32 struck = spawn("wolf", 4, 0);
        const quint32 near = spawn("wolf", 8, 4);
        const quint32 far = spawn("wolf", 4, 25);
        const quint32 dog = pet("dog", -5, 5);
        strike(struck);
        ticks(1);
        for (const quint32 wolf : {struck, near}) {
            QCOMPARE(mob(wolf)->provokedBy, player().id);
        }
        QCOMPARE(mob(far)->provokedBy, 0u);
        QCOMPARE(mob(dog)->provokedBy, 0u);
        QVERIFY(mob(struck)->provokedBy != bystander.id);
    }

    void anOcelotRunsFromPlayersUnlessCalmed()
    {
        joinArena();
        const quint32 ocelot = spawn("ocelot", 4, 0);
        player().motion.position = at(0, 0);
        hold(nullptr);
        ticks(40);
        QVERIFY2(distance(ocelot) > 6.0, qPrintable(QString::number(distance(ocelot))));

        // Holding raw cod and standing still: it no longer flees.
        const quint32 calm = spawn("ocelot", 0, -4);
        hold("qtcraft:raw_cod");
        player().motion.velocity = {};
        ticks(100);
        QCOMPARE(mob(calm)->fleeTimer, 0.0);
        QcMobManager::PlayerView view;
        view.held = item("qtcraft:raw_cod");
        view.speed = 1.0;
        QVERIFY(QcMobFear::calms(view, *kind("ocelot").shy, *m_content));
        view.speed = 4.0;
        QVERIFY(!QcMobFear::calms(view, *kind("ocelot").shy, *m_content));
        view.held = item("qtcraft:bone");
        view.speed = 0.0;
        QVERIFY(!QcMobFear::calms(view, *kind("ocelot").shy, *m_content));
    }

    void creaturesScaredOfCatsKeepAway()
    {
        // No creature of the base game is scared of cats; a mod makes the zombie so.
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcMobDefinition zombie = kind("zombie");
        zombie.scaredOfCats = true;
        content->registerMob(zombie);
        m_content = content;
        joinArena(midnight);
        const quint32 cat = spawn("cat", 0, 6);
        const quint32 scared = spawn("zombie", 0, 3);
        player().motion.position = at(0, -20);
        ticks(40);
        QVERIFY2((mob(scared)->motion.position - mob(cat)->motion.position).length() > QcMobFear::catDistance - 0.5,
                 qPrintable(QString::number((mob(scared)->motion.position - mob(cat)->motion.position).length())));
    }

    // Debug-relative measurement (decision E-480): the mean time of a server tick while a player walks
    // a square, first alone, then with 20 dogs following; prints both.
    void twentyDogsFollowAMovingPlayer()
    {
        joinArena();
        const double alone = walkSquare();
        QList<quint32> dogs;
        for (int i = 0; i < 20; ++i) {
            const int row = i / 5;
            dogs.append(pet("dog", -8 + (i % 5) * 4, -8 + row * 4));
        }
        const double withDogs = walkSquare();
        int close = 0;
        for (const quint32 dog : std::as_const(dogs)) {
            close += distance(dog) <= kind("dog").tameable->teleportDistance ? 1 : 0;
        }
        qInfo().noquote() << u"MEASURE pets: %1 ms per tick alone, %2 ms with 20 dogs following (debug), %3 within the teleport distance"_s
                                 .arg(alone, 0, 'f', 3)
                                 .arg(withDogs, 0, 'f', 3)
                                 .arg(close);
        QCOMPARE(close, 20);
    }

private:
    // Walks the player around a square of 16 m at 4 m/s for 400 ticks; the mean milliseconds per tick.
    double walkSquare()
    {
        const int steps = 400;
        QElapsedTimer timer;
        timer.start();
        for (int tick = 0; tick < steps; ++tick) {
            const double along = std::fmod(tick * 0.2, 64.0);
            const double side = std::fmod(along, 16.0) - 8.0;
            const int edge = static_cast<int>(along / 16.0);
            player().motion.position = edge == 0 ? at(side, -8) : edge == 1 ? at(8, side) : edge == 2 ? at(-side, 8) : at(-8, -side);
            ticks(1);
        }
        return static_cast<double>(timer.nsecsElapsed()) / 1e6 / steps;
    }
};

QTEST_GUILESS_MAIN(TestPetRules)
#include "test_pet_rules.moc"
