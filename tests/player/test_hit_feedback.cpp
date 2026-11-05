// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/physics/QcPhysics.h"
#include "fixtures/QcServerTestFixture.h"
#include "server/animals/QcMobFamilies.h"
#include "server/entities/QcHurtWindow.h"
#include "server/explosions/QcServerExplosions.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/mobs/QcMobManager.h"
#include "server/projectiles/QcProjectileManager.h"

#include <cmath>

using namespace Qt::StringLiterals;

// The server's side of hit feedback (decision E-971), ticked by hand: a player's blow pushes a
// creature away from the player, twice as far while sprinting; a blow on the player pushes him away
// from the attacker, sends the push as his state and tells the client the fresh hit with the
// direction it came from; hits without a direction tell none, hits in the short invulnerability
// tell nothing. Hurts from the phases after the creatures' tick (arrows, explosions) still reach
// the reactions at the start of the next creatures' tick (silverfish kin, family splits).
class TestHitFeedback : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcServerClient &player() { return *m_server->entities().clients().front(); }

    // A pig `east` blocks east of the player, saved with the world so it never despawns.
    quint32 pig(int east = 2)
    {
        const QcCreatureKindId type = *m_content->mobKind(u"qtcraft:pig"_s);
        const QcVec3 at{m_feet.x + east + 0.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        return m_server->mobs().spawn(type, at, m_server->entities().newId(), false, true);
    }

    // The velocity of the pig `mob` right after the player hit it, `sprinting` or not.
    QcVec3 hitPig(quint32 mob, bool sprinting)
    {
        player().sprinting = sprinting;
        send(QcMsg::AttackEntity{mob});
        const QcCreature *hit = m_server->mobs().find(mob);
        return hit ? hit->motion.velocity : QcVec3{};
    }

    // The health messages that told the raw client of a fresh hit, oldest first.
    QList<QcMsg::PlayerHealth> hurts() const
    {
        QList<QcMsg::PlayerHealth> found;
        for (const QcMsg::PlayerHealth &health : std::as_const(m_client->healths)) {
            if (health.hurt) {
                found.append(health);
            }
        }
        return found;
    }


    // How a creature is hurt inside the server's ticks, after the creatures' own tick.
    enum class Strike { Arrow, Blast };

    // A grass floor (no stone a silverfish could hide in) with open air above, without the
    // creatures the chunks brought; explosions break no blocks.
    void joinFloor()
    {
        join();
        chat(u"/gamemode creative"_s);
        chat(u"/gamerule explosionBlockDamage false"_s);
        loadAround(m_feet, 1);
        m_server->mobs().setNaturalSpawning(false);
        for (int dx = -12; dx <= 12; ++dx) {
            for (int dz = -12; dz <= 12; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:grass_block"));
                for (int dy = 0; dy <= 5; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        m_server->mobs().removeAll();
    }

    // A still creature `name` `east` blocks east of the player, with health enough to survive.
    quint32 standing(const QString &name, int east)
    {
        const QcVec3 at{m_feet.x + east + 0.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        const quint32 mob = m_server->mobs().spawn(*m_content->mobKind(name), at, m_server->entities().newId());
        m_server->mobs().find(mob)->noAi = true;
        m_server->mobs().find(mob)->health = 1000.0;
        return mob;
    }

    // Hurts the creature `mob` by `strike` within real ticks (the projectile or the explosion
    // phase) and runs one tick more for the reactions. Returns whether it was hurt.
    bool strikeInTicks(quint32 mob, Strike strike)
    {
        const QcVec3 at = m_server->mobs().find(mob)->motion.position;
        if (strike == Strike::Arrow) {
            QcProjectileManager::Launch launch;
            launch.type = u"qtcraft:arrow"_s;
            launch.position = at + QcVec3{0.0, 3.0, 0.0};
            launch.velocity = {0.0, -20.0, 0.0};
            if (!m_server->entities().projectiles().launch(launch, m_server->entities().newId())) {
                return false;
            }
        } else {
            const QcBlockPos cell = QcPhysics::blockContaining(at).offset(0, 0, 2);
            m_server->world().setBlock(cell, id("qtcraft:explosive"));
            if (!m_server->systems().explosions.light(cell, 1)) {
                return false;
            }
        }

        for (int tick = 0; tick < 40; ++tick) {
            ticks(1);
            const QcCreature *hurt = m_server->mobs().find(mob);
            if (hurt && hurt->health < 1000.0) {
                ticks(1);
                return true;
            }
        }
        return false;
    }

    static void strikes()
    {
        QTest::addColumn<bool>("arrow");
        QTest::newRow("arrow") << true;
        QTest::newRow("blast") << false;
    }

private slots:
    // The blow lands between two ticks, on a pig standing on the ground: the push must carry it
    // through the following steps, and the hurt must last until the clients were told.
    void aStandingCreatureIsThrownAndFlashesForTheClients()
    {
        join();
        const quint32 mob = pig();
        ticks(20);
        QVERIFY(m_server->mobs().find(mob)->motion.onGround);
        const double startX = m_server->mobs().find(mob)->motion.position.x;

        m_client->connection.send(QcMsg::AttackEntity{mob});
        QcTestUtil::pump();
        QVERIFY(m_server->mobs().find(mob)->hurtUnsent);
        QVERIFY(!m_server->mobs().find(mob)->motion.onGround);
        // The creatures' tick before the sync keeps the hurt; the sync of the tick forgets it.
        m_server->mobs().tick(m_server->world(), {}, 0.0, QcPhysics::tickDuration, [this] { return m_server->entities().newId(); });
        QVERIFY(m_server->mobs().find(mob)->hurtUnsent);
        ticks(1);
        QVERIFY(!m_server->mobs().find(mob)->hurtUnsent);

        ticks(10);
        const double thrown = m_server->mobs().find(mob)->motion.position.x - startX;
        QVERIFY2(thrown > 0.8, qPrintable(QString::number(thrown)));
    }

    void aBlowPushesTheCreatureAwayAndASprintTwiceAsFar()
    {
        join();
        const quint32 first = pig();
        const QcVec3 walking = hitPig(first, false);
        // East, away from the player, and up; the push's own lift.
        QVERIFY2(walking.x > 0.5 * QcMobManager::knockbackSpeed, qPrintable(QString::number(walking.x)));
        QVERIFY(std::abs(walking.z) < 1e-9);
        QVERIFY(walking.y > 0.0);

        // The same blow from a sprint, on a second pig at the same place once the cooldown is over.
        m_server->mobs().find(first)->motion.position.z += 40.0;
        ticks(10);
        const quint32 second = pig();
        const QcVec3 sprinting = hitPig(second, true);
        QVERIFY2(std::abs(sprinting.x / walking.x - QcServerCombat::sprintKnockback) < 0.05,
                 qPrintable(u"%1 against %2"_s.arg(sprinting.x).arg(walking.x)));
        QCOMPARE(sprinting.y, walking.y);
    }

    void aBlowOnThePlayerPushesAwayAndTellsWhereItCameFrom()
    {
        join();
        ticks(2);
        const qsizetype hurtsBefore = hurts().size();

        // A creature west of the player strikes: the push goes east.
        const QcHurtWindow::Take take = m_server->systems().health.damage(player(), 2.0, {1.0, 0.0, 0.0}, QcPlayerHealth::DamageKind::Blow);
        QCOMPARE(take, QcHurtWindow::Take::Fresh);
        QCOMPARE(player().motion.velocity.x, QcMobManager::knockbackSpeed);
        QCOMPARE(player().motion.velocity.y, QcMobManager::knockbackLift);
        QVERIFY(!player().motion.onGround);
        QcTestUtil::pump();
        QCOMPARE(hurts().size(), hurtsBefore + 1);
        QVERIFY(hurts().constLast().hurtFrom.has_value());
        QVERIFY(std::abs(*hurts().constLast().hurtFrom - 270.0f) < 1e-3f);

        // The push reaches the client as its state, for the prediction to replay on.
        ticks(1);
        QVERIFY2(m_client->motion.velocity.x > 0.0, qPrintable(QString::number(m_client->motion.velocity.x)));

        // A hit during the short invulnerability is no fresh hit: no tilt, no push.
        const QcVec3 before = player().motion.velocity;
        m_server->systems().health.damage(player(), 3.0, {0.0, 0.0, 1.0}, QcPlayerHealth::DamageKind::Blow);
        QcTestUtil::pump();
        QCOMPARE(hurts().size(), hurtsBefore + 1);
        QCOMPARE(player().motion.velocity, before);

        // A hit from the north (pushed south) after the window.
        ticks(QcHurtWindow::defaultTicks + 1);
        m_server->systems().health.damage(player(), 1.0, {0.0, 0.0, 1.0}, QcPlayerHealth::DamageKind::Blow);
        QcTestUtil::pump();
        QCOMPARE(hurts().size(), hurtsBefore + 2);
        QVERIFY(std::abs(*hurts().constLast().hurtFrom - 0.0f) < 1e-3f);
    }

    void aHitWithoutADirectionTellsNone()
    {
        join();
        ticks(2);
        const qsizetype hurtsBefore = hurts().size();
        m_server->systems().health.damage(player(), 2.0, {}, QcPlayerHealth::DamageKind::Unblockable);
        QcTestUtil::pump();
        QCOMPARE(hurts().size(), hurtsBefore + 1);
        QVERIFY(!hurts().constLast().hurtFrom);
        QCOMPARE(player().motion.velocity.x, 0.0);

        // Messages that tell the health for other reasons are no hit.
        m_server->systems().health.sendHealth(player());
        QcTestUtil::pump();
        QCOMPARE(hurts().size(), hurtsBefore + 1);
    }
    void anArrowOrABlastWakesTheSilverfishKin_data() { strikes(); }
    void anArrowOrABlastWakesTheSilverfishKin()
    {
        QFETCH(bool, arrow);
        const Strike strike = arrow ? Strike::Arrow : Strike::Blast;
        joinFloor();
        const QcBlockId kin = id("qtcraft:infested_stone_bricks");
        QList<QcBlockPos> infested;
        for (int dz = -6; dz <= 6; dz += 3) {
            infested.append(m_feet.offset(5, -1, dz));
            m_server->world().setBlock(infested.last(), kin);
        }
        const quint32 bitten = standing(u"qtcraft:silverfish"_s, 3);
        QVERIFY(strikeInTicks(bitten, strike));
        // Without a mind the silverfish may not change the world: a woken block turns back into its
        // base block instead of breaking (QcInfestation::wake).
        const auto woken = std::ranges::count_if(infested, [this, kin](QcBlockPos pos) { return *m_server->world().blockAt(pos) != kin; });
        QVERIFY2(woken >= 1, qPrintable(QString::number(woken)));
    }

    void anArrowOrABlastSplitsAFamilyCreature_data() { strikes(); }
    void anArrowOrABlastSplitsAFamilyCreature()
    {
        QFETCH(bool, arrow);
        const Strike strike = arrow ? Strike::Arrow : Strike::Blast;
        joinFloor();
        const quint32 parent = standing(u"qtcraft:kjut"_s, 3);
        QVERIFY(QcMobFamilies::childrenOf(m_server->mobs(), parent).isEmpty());
        QVERIFY(strikeInTicks(parent, strike));
        QVERIFY(!QcMobFamilies::childrenOf(m_server->mobs(), parent).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestHitFeedback)
#include "test_hit_feedback.moc"
