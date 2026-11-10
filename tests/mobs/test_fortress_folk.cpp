// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/mobs/QcMobSerializer.h"
#include "fixtures/QcServerTestFixture.h"
#include "server/core/QcServerWorld.h"
#include "server/gameplay/QcServerEffects.h"
#include "server/mobbrains/QcMobSteering.h"
#include "server/projectiles/QcProjectileManager.h"
#include "server/spawners/QcSpawners.h"

using namespace Qt::StringLiterals;

// The folk of the fortress (decisions E-1056 to E-1059): the ember wraith that rises to its target
// and fires in volleys, the charred skeleton whose blows wither, what they leave and the fireball
// item. Ticks are stepped by hand.
class TestFortressFolk : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcCreatureKindId kind(const QString &name) const { return *m_content->mobKind(u"qtcraft:"_s + name); }
    QcServerClient &player() { return *m_server->entities().clients().front(); }

    void joinStage()
    {
        join();
        loadAround(m_feet, 2);
        chat(u"/gamemode survival"_s);
        m_server->mobs().setNaturalSpawning(false);
        m_server->mobs().removeAll();
    }

    quint32 spawn(const QString &name, double dx, double dz, double dy = 0.0)
    {
        const QcVec3 at{m_feet.x + dx + 0.5, m_feet.y + dy, m_feet.z + dz + 0.5};
        return m_server->mobs().spawn(kind(name), at, m_server->entities().newId());
    }

    // A floor of stone `size` blocks to every side, with air above it up to `height`.
    void clearStage(int size, int height)
    {
        for (int dx = -size; dx <= size; ++dx) {
            for (int dz = -size; dz <= size; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:stone"));
                for (int dy = 0; dy <= height; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
    }

    void hold(const char *name, int count = 1)
    {
        chat(u"/give "_s + QString::fromLatin1(name).section(u':', -1) + u" "_s + QString::number(count));
        send(QcMsg::SelectHotbarSlot{slotOf(item(name))});
        ticks(1);
    }

    QList<quint32> projectileIds() const
    {
        QList<quint32> ids;
        for (const QcProjectile &projectile : m_server->entities().projectiles().all()) {
            ids.append(projectile.id);
        }
        return ids;
    }

    // The ticks at which new projectiles appear over `count` ticks; the ones that flew off go.
    QList<int> shotTicks(int count)
    {
        QList<int> shots;
        QList<quint32> known = projectileIds();
        for (int tick = 0; tick < count; ++tick) {
            ticks(1);
            player().health = QcServerClient::maxHealth;
            const QList<quint32> flying = projectileIds();
            for (const quint32 id : flying) {
                if (!known.contains(id)) {
                    known.append(id);
                    shots.append(tick);
                }
            }
        }
        return shots;
    }

    // The items lying about, by name.
    QHash<QString, int> itemsAround() const
    {
        QHash<QString, int> left;
        for (const QcItemEntity &drop : m_server->entities().items().items()) {
            left[m_content->item(drop.stack.item).name] += drop.stack.count;
        }
        return left;
    }

    // A blow of the player, or else an arrow of nobody, kills a creature of `name` with one health
    // point left.
    void kill(const QString &name, bool byPlayer)
    {
        const quint32 id = spawn(name, 2, 2);
        QcCreature *mob = m_server->mobs().find(id);
        mob->noAi = true;
        mob->health = 1.0;
        if (byPlayer) {
            player().attackCooldown = 0.0;
            send(QcMsg::AttackEntity{id});
        } else {
            const QcVec3 at = mob->motion.position + QcVec3{0.0, 1.0, 1.2};
            m_server->entities().projectiles().launch({0, u"qtcraft:arrow"_s, at, {0.0, 0.0, -25.0}, QcProjectilePickup::Never, 1.0},
                                                      m_server->entities().newId());
        }
        ticks(3);
        if (m_server->mobs().find(id)) {
            QcTestUtil::failCase(u"the %1 survived"_s.arg(name));
        }
    }

    // What `count` kills of `name` leave, more than what lay about before.
    QHash<QString, int> kills(const QString &name, int count, bool byPlayer)
    {
        QHash<QString, int> left = itemsAround();
        for (auto it = left.begin(); it != left.end(); ++it) {
            it.value() = -it.value();
        }
        for (int i = 0; i < count; ++i) {
            kill(name, byPlayer);
        }
        const QHash<QString, int> after = itemsAround();
        for (auto it = after.cbegin(); it != after.cend(); ++it) {
            left[it.key()] += it.value();
        }
        left.removeIf([](const auto &entry) { return entry.value() == 0; });
        return left;
    }

private slots:
    // The role model's numbers (decision E-1056): health, size, speeds and reach.
    void theFolkKeepTheirNumbers()
    {
        const QcMobDefinition &wraith = m_content->mobs()[kind(u"ember_wraith"_s)];
        QCOMPARE(wraith.maxHealth, 20.0);
        QCOMPARE(wraith.attackDamage, 6.0);
        QCOMPARE(wraith.sightRange, 48.0);
        QCOMPARE(wraith.width, 0.6);
        QCOMPARE(wraith.height, 1.8);
        QVERIFY(wraith.fireImmune);
        QCOMPARE(wraith.archer.volley, 3);
        const QcMobDefinition &charred = m_content->mobs()[kind(u"charred_skeleton"_s)];
        QCOMPARE(charred.maxHealth, 20.0);
        QCOMPARE(charred.attackDamage, 8.0);
        QCOMPARE(charred.height, 2.4);
        QCOMPARE(charred.heldItem, u"qtcraft:stone_sword"_s);
        QVERIFY(charred.fireImmune);
        QVERIFY(charred.undead);
    }

    // Three seconds of flame, three shots 0.3 s apart, then 5.3 s of rest and the next charge: as
    // in the role model, 60, 6, 6, 6 and 100 ticks.
    void anEmberWraithFiresVolleysOnTheRoleModelsBeat()
    {
        joinStage();
        clearStage(14, 8);
        const quint32 wraith = spawn(u"ember_wraith"_s, 0, 12);
        chat(u"/gamerule mobGriefing false"_s);
        const QList<int> shots = shotTicks(60 + 12 + 166 + 12 + 4);
        qInfo() << "shots at ticks" << shots;
        QCOMPARE(shots.size(), qsizetype{6});
        QCOMPARE(shots[1] - shots[0], 6);
        QCOMPARE(shots[2] - shots[1], 6);
        QCOMPARE(shots[3] - shots[2], 166);
        QCOMPARE(shots[4] - shots[3], 6);
        QVERIFY2(std::abs(shots[0] - 60) <= 4, "the first shot comes three seconds after the sight");
        QVERIFY(m_server->mobs().find(wraith));
    }

    // It flares from the charge to its last shot, with the cry at the start only.
    void anEmberWraithFlaresWhileItCharges()
    {
        joinStage();
        clearStage(14, 8);
        const quint32 wraith = spawn(u"ember_wraith"_s, 0, 12);
        const QString cry = m_content->mobs()[kind(u"ember_wraith"_s)].archer.warnSound;
        int flaring = 0;
        for (int tick = 0; tick < 150; ++tick) {
            ticks(1);
            player().health = QcServerClient::maxHealth;
            QcTestUtil::pump();
            flaring += m_server->mobs().find(wraith)->drifter->flares ? 1 : 0;
        }
        QVERIFY2(flaring >= 70 && flaring <= 74, qPrintable(u"flares for %1 ticks"_s.arg(flaring)));
        QCOMPARE(m_client->sounds.count(cry), 1);
        QVERIFY(m_client->flags.value(wraith) & QcMsg::Burning);
    }

    // Closer than the muzzle's distance a shot still leaves along the line to the target, at its
    // full speed.
    void aWailerClosebyShootsTowardsItsTarget()
    {
        joinStage();
        clearStage(9, 8);
        // Its own manager: a real target stands where the shot starts and takes it at once.
        QcMobManager mobs(m_content, 9);
        mobs.setNaturalSpawning(false);
        const QcVec3 target = player().motion.position;
        const quint32 wailer = mobs.spawn(kind(u"wailer"_s), target + QcVec3{0.0, 0.0, 2.0}, 700000);
        const QList<QcMobManager::PlayerView> players{{.entityId = 900000, .position = target}};
        quint32 nextId = 700001;
        QList<QcProjectileManager::Launch> shots;
        for (int tick = 0; tick < 60 && shots.isEmpty(); ++tick) {
            mobs.find(wailer)->motion.position = target + QcVec3{0.0, 0.0, 2.0};
            mobs.tick(m_server->world(), players, 0.0, QcPhysics::tickDuration, [&nextId] { return nextId++; });
            shots = mobs.takeLaunches();
        }
        QVERIFY(!shots.isEmpty());
        const QcVec3 velocity = shots.constFirst().velocity;
        const double speed = m_content->mobs()[kind(u"wailer"_s)].archer.speed;
        QVERIFY2(std::abs(velocity.length() - speed) < 0.5, qPrintable(QString::number(velocity.length())));
        QVERIFY2(velocity.z < 0.0, qPrintable(u"shot away from the target: %1"_s.arg(velocity.z)));
    }

    // A hit is 5 damage and 5 seconds of fire, as in the role model.
    void aSmallFireballBurns()
    {
        joinStage();
        clearStage(6, 4);
        const QcVec3 eyes = player().motion.position + QcVec3{0.0, 1.0, 0.0};
        QVERIFY(m_server->entities().projectiles().launch({0, u"qtcraft:small_fireball"_s, eyes + QcVec3{0.0, 0.0, 3.0}, {0.0, 0.0, -16.0},
                                                           QcProjectilePickup::Never, 1.0}, m_server->entities().newId()));
        ticks(10);
        QCOMPARE(player().health, QcServerClient::maxHealth - 5.0);
        QVERIFY(player().burnTicks > 80);
    }

    // Fire resistance takes the shot's harm, as a fire-proof body has none to fear from it.
    void aFireProofBodyTakesNothingFromTheShot()
    {
        joinStage();
        clearStage(6, 4);
        QcServerEffects::givePlayer(*m_content, player(), {u"qtcraft:fire_resistance"_s, 1, 400});
        // No hit at all: no hurt window, and the fire it has burns on with the time it had.
        player().burnTicks = 40;
        const QcVec3 eyes = player().motion.position + QcVec3{0.0, 1.0, 0.0};
        QVERIFY(m_server->entities().projectiles().launch({0, u"qtcraft:small_fireball"_s, eyes + QcVec3{0.0, 0.0, 3.0}, {0.0, 0.0, -16.0},
                                                           QcProjectilePickup::Never, 1.0}, m_server->entities().newId()));
        ticks(10);
        QCOMPARE(player().health, QcServerClient::maxHealth);
        QCOMPARE(player().hurt.wouldTake(1.0), QcHurtWindow::Take::Fresh);
        QVERIFY(player().burnTicks <= 40);
        QVERIFY(m_server->entities().projectiles().all().isEmpty());

        // Nor do fellow wraiths mind it.
        const quint32 friendId = spawn(u"ember_wraith"_s, 0, 3);
        m_server->mobs().find(friendId)->noAi = true;
        const QcVec3 at = m_server->mobs().find(friendId)->motion.position + QcVec3{0.0, 1.0, 2.0};
        QVERIFY(m_server->entities().projectiles().launch({0, u"qtcraft:small_fireball"_s, at, {0.0, 0.0, -16.0}, QcProjectilePickup::Never, 1.0},
                                                          m_server->entities().newId()));
        ticks(10);
        QcTestUtil::pump();
        QCOMPARE(m_server->mobs().find(friendId)->health, 20.0);
        QVERIFY(!m_client->hurt.contains(friendId));
        QCOMPARE(m_client->sounds.count(m_content->mobs()[kind(u"ember_wraith"_s)].hurtSound), 0);
        QVERIFY(m_server->entities().projectiles().all().isEmpty());
    }

    // On a block it lights the cell in front, a creature's shot only under mobGriefing.
    void aShotLightsFireUnderTheRule()
    {
        joinStage();
        clearStage(8, 5);
        const QcBlockPos wall = m_feet.offset(0, 0, 6);
        m_server->world().setBlock(wall, id("qtcraft:netherrack"));
        const QcVec3 from{wall.x + 0.5, wall.y + 0.5, wall.z - 2.5};
        const quint32 shooter = spawn(u"ember_wraith"_s, 3, 3);
        m_server->mobs().find(shooter)->noAi = true;

        chat(u"/gamerule mobGriefing false"_s);
        QVERIFY(m_server->entities().projectiles().launch({shooter, u"qtcraft:small_fireball"_s, from, {0.0, 0.0, 16.0}, QcProjectilePickup::Never, 1.0},
                                                          m_server->entities().newId()));
        ticks(8);
        QCOMPARE(m_server->world().blockAt(wall.offset(0, 0, -1)), qcAirBlockId);

        // A shooter that is gone counts as the creature it most likely was.
        const quint32 gone = spawn(u"ember_wraith"_s, 3, 2);
        QVERIFY(m_server->mobs().remove(gone));
        QVERIFY(m_server->entities().projectiles().launch({gone, u"qtcraft:small_fireball"_s, from, {0.0, 0.0, 16.0}, QcProjectilePickup::Never, 1.0},
                                                          m_server->entities().newId()));
        ticks(8);
        QCOMPARE(m_server->world().blockAt(wall.offset(0, 0, -1)), qcAirBlockId);

        // A player's shot and one of nobody (a dispenser) set the rule aside.
        for (const quint32 free : {player().id, quint32{0}}) {
            QVERIFY(m_server->entities().projectiles().launch({free, u"qtcraft:small_fireball"_s, from, {0.0, 0.0, 16.0}, QcProjectilePickup::Never, 1.0},
                                                              m_server->entities().newId()));
            ticks(8);
            QCOMPARE(m_server->world().blockAt(wall.offset(0, 0, -1)), id("qtcraft:fire"));
            m_server->world().setBlock(wall.offset(0, 0, -1), qcAirBlockId);
        }

        chat(u"/gamerule mobGriefing true"_s);
        QVERIFY(m_server->entities().projectiles().launch({shooter, u"qtcraft:small_fireball"_s, from, {0.0, 0.0, 16.0}, QcProjectilePickup::Never, 1.0},
                                                          m_server->entities().newId()));
        ticks(8);
        QCOMPARE(m_server->world().blockAt(wall.offset(0, 0, -1)), id("qtcraft:fire"));
    }

    // With a target it stops and its eyes settle near the target's, below or above it by a draw
    // of 0.5 +- 3 metres; a target far above draws it up.
    void anEmberWraithRisesToItsTarget()
    {
        joinStage();
        clearStage(10, 22);
        const quint32 wraith = spawn(u"ember_wraith"_s, 0, 8);
        const QcMobDefinition &definition = m_content->mobs()[kind(u"ember_wraith"_s)];
        // The player on a pillar, out of reach.
        for (int dy = 0; dy < 12; ++dy) {
            m_server->world().setBlock(m_feet.offset(0, dy, 0), id("qtcraft:stone"));
        }
        player().motion.position.y = m_feet.y + 12.0;
        player().stateDirty = true;
        double highest = m_server->mobs().find(wraith)->motion.position.y;
        double fastest = 0.0;
        const double start = m_server->mobs().find(wraith)->motion.position.x;
        double wandered = 0.0;
        const double eyeHeight = definition.height * QcMobSteering::eyeShare;
        const double targetEyes = player().motion.position.y + QcPhysics::playerEyeHeight;
        double nearest = 1000.0;
        for (int i = 0; i < 320; ++i) {
            const double before = m_server->mobs().find(wraith)->motion.position.y;
            ticks(1);
            player().health = QcServerClient::maxHealth;
            const QcCreature &body = *m_server->mobs().find(wraith);
            highest = std::max(highest, body.motion.position.y);
            fastest = std::max(fastest, (body.motion.position.y - before) * 20.0);
            wandered = std::max(wandered, std::abs(body.motion.position.x - start));
            if (i >= 200) {
                nearest = std::min(nearest, std::abs(body.motion.position.y + eyeHeight - (targetEyes - body.drifter->heightOffset)));
            }
        }
        qInfo() << "rose to" << highest - m_feet.y << "fastest" << fastest << "m/s, wandered" << wandered;
        QVERIFY2(highest - m_feet.y > 5.0, "it did not rise towards the target");
        QVERIFY(fastest <= definition.hover->climbSpeed + 0.01);
        QVERIFY2(wandered < 0.5, "it left its place");
        QVERIFY2(nearest < 0.2, qPrintable(u"its eyes stayed %1 m from where they belong"_s.arg(nearest)));
    }

    // Close by it presses on and strikes for 6, once a second, without firing.
    void anEmberWraithStrikesFromCloseBy()
    {
        joinStage();
        clearStage(6, 4);
        spawn(u"ember_wraith"_s, 0, 1.2);
        double lowest = QcServerClient::maxHealth;
        for (int i = 0; i < 30 && lowest == QcServerClient::maxHealth; ++i) {
            ticks(1);
            lowest = std::min(lowest, player().health);
        }
        QCOMPARE(lowest, QcServerClient::maxHealth - 6.0);
        QVERIFY(m_server->entities().projectiles().all().isEmpty());
    }

    // Without a target it rests and wanders on the ground, and stops at an edge.
    void anEmberWraithStaysOnItsBridge()
    {
        joinStage();
        chat(u"/gamemode creative"_s);
        clearStage(9, 3);
        // A deck five wide over a gap of eight, far from the player's stage.
        for (int dx = -9; dx <= 9; ++dx) {
            for (int dz = 3; dz <= 9; ++dz) {
                for (int dy = -9; dy <= -1; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        for (int dx = -9; dx <= 9; ++dx) {
            for (int dz = 4; dz <= 8; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:stone"));
            }
        }
        const quint32 wraith = spawn(u"ember_wraith"_s, 0, 6);
        bool moved = false;
        const QcVec3 start = m_server->mobs().find(wraith)->motion.position;
        for (int i = 0; i < 1200; ++i) {
            ticks(1);
            const QcVec3 now = m_server->mobs().find(wraith)->motion.position;
            moved = moved || (now - start).length() > 1.0;
            QVERIFY2(now.y >= m_feet.y - 0.1, qPrintable(u"it fell off at %1 %2 %3"_s.arg(now.x).arg(now.y).arg(now.z)));
        }
        QVERIFY(moved);
    }

    // On a spot with an edge all around a rest lasts as long as anywhere: 3 to 9 seconds.
    void anEmberWraithRestsItsFullTimeAtAnEdge()
    {
        joinStage();
        chat(u"/gamemode creative"_s);
        clearStage(9, 3);
        for (int dx = -4; dx <= 4; ++dx) {
            for (int dz = 2; dz <= 9; ++dz) {
                for (int dy = -9; dy <= -1; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        m_server->world().setBlock(m_feet.offset(0, -1, 6), id("qtcraft:stone"));
        const quint32 wraith = spawn(u"ember_wraith"_s, 0, 6);
        const int shortest = static_cast<int>(0.5 * m_content->mobs()[kind(u"ember_wraith"_s)].drift.restSeconds * QcPhysics::tickRate);
        QList<int> rests;
        int length = 0;
        for (int i = 0; i < 1500; ++i) {
            ticks(1);
            const bool resting = m_server->mobs().find(wraith)->drifter->resting;
            if (!resting && length > 0) {
                rests.append(length);
            }
            length = resting ? length + 1 : 0;
        }
        qInfo() << "rests" << rests;
        QVERIFY(rests.size() >= 2);
        QVERIFY2(std::ranges::min(rests) >= shortest - 1, qPrintable(QString::number(std::ranges::min(rests))));
    }

    void waterAndSnowHurtAnEmberWraith()
    {
        joinStage();
        clearStage(6, 4);
        const quint32 wet = spawn(u"ember_wraith"_s, 3, 3);
        m_server->mobs().find(wet)->noAi = true;
        m_server->world().setBlock(m_feet.offset(3, 0, 3), id("qtcraft:water"));
        m_server->world().setBlock(m_feet.offset(3, 1, 3), id("qtcraft:water"));
        ticks(60);
        QVERIFY(m_server->mobs().find(wet)->health < 20.0);

        // A player's snowball deals 3.
        const quint32 snowed = spawn(u"ember_wraith"_s, -3, 3);
        m_server->mobs().find(snowed)->noAi = true;
        const QcVec3 at = m_server->mobs().find(snowed)->motion.position + QcVec3{0.0, 1.0, 2.0};
        QVERIFY(m_server->entities().projectiles().launch({player().id, u"qtcraft:snowball"_s, at, {0.0, 0.0, -20.0}, QcProjectilePickup::Never, 1.0},
                                                          m_server->entities().newId()));
        ticks(6);
        QCOMPARE(m_server->mobs().find(snowed)->health, 17.0);
    }

    // Only a player's kill leaves a rod, about every other one.
    void anEmberRodIsAPlayersFind()
    {
        joinStage();
        clearStage(6, 4);
        const QHash<QString, int> byPlayer = kills(u"ember_wraith"_s, 60, true);
        const int rods = byPlayer.value(u"qtcraft:ember_rod"_s);
        qInfo() << "rods of 60 kills" << rods;
        QVERIFY2(rods >= 15 && rods <= 45, qPrintable(QString::number(rods)));
        QVERIFY(kills(u"ember_wraith"_s, 60, false).isEmpty());
    }

    void aSpawnerBringsForthEmberWraiths()
    {
        joinStage();
        clearStage(6, 8);
        const QcBlockPos cage = m_feet.offset(3, 1, 3);
        chat(u"/setblock %1 %2 %3 spawner ember_wraith"_s.arg(cage.x).arg(cage.y).arg(cage.z));
        ticks(1);
        QCOMPARE(m_server->world().spawners().at(cage)->mob, u"qtcraft:ember_wraith"_s);

        // The fortress's cage works on a kind that never comes by itself (spawn weight 0).
        bool came = false;
        for (int tick = 0; tick < 1200 && !came; ++tick) {
            ticks(1);
            player().health = QcServerClient::maxHealth;
            came = std::ranges::any_of(m_server->mobs().mobs(), [this](const QcCreature &mob) { return mob.type == kind(u"ember_wraith"_s) && mob.spawner; });
        }
        QVERIFY(came);
    }

    // The blow takes 8 and gives the wither effect for 10 seconds; the kind is immune to it.
    void aCharredBlowWithers()
    {
        joinStage();
        clearStage(6, 4);
        spawn(u"charred_skeleton"_s, 0, 1.2);
        for (int i = 0; i < 30 && player().health == QcServerClient::maxHealth; ++i) {
            ticks(1);
        }
        QCOMPARE(player().health, QcServerClient::maxHealth - 8.0);
        const QcStatusEffect *withering = player().effects.find(u"qtcraft:wither"_s);
        QVERIFY(withering);
        QVERIFY(withering->ticksLeft > 180 && withering->ticksLeft <= 200);
        QCOMPARE(withering->level, 1);

        const QcMobDefinition &charred = m_content->mobs()[kind(u"charred_skeleton"_s)];
        QVERIFY(charred.immuneTo.contains(u"qtcraft:wither"_s));
    }

    // Coal one in three, 0 to 2 bones; the skull and the sword only for a player, and seldom.
    void aCharredSkeletonLeavesWhatTheRoleModelSays()
    {
        joinStage();
        clearStage(6, 4);
        const QcMobDefinition &charred = m_content->mobs()[kind(u"charred_skeleton"_s)];
        const auto drop = [&charred](const QString &item) { return *std::ranges::find(charred.drops, item, &QcItemDrop::item); };
        QVERIFY(qAbs(drop(u"qtcraft:coal"_s).chance - 1.0 / 3.0) < 0.001);
        QCOMPARE(drop(u"qtcraft:charred_skull"_s).chance, 0.025);
        QVERIFY(drop(u"qtcraft:charred_skull"_s).byPlayer);
        QCOMPARE(drop(u"qtcraft:stone_sword"_s).chance, 0.085);
        QVERIFY(drop(u"qtcraft:stone_sword"_s).byPlayer);

        const QHash<QString, int> left = kills(u"charred_skeleton"_s, 60, false);
        qInfo() << left;
        const int bones = left.value(u"qtcraft:bone"_s);
        QVERIFY2(bones >= 40 && bones <= 80, qPrintable(QString::number(bones)));
        QVERIFY(left.value(u"qtcraft:coal"_s) >= 8);
        QVERIFY(!left.contains(u"qtcraft:charred_skull"_s));
        QVERIFY(!left.contains(u"qtcraft:stone_sword"_s));
    }

    // A rod grinds into two dust, which with gunpowder and any coal makes three fireballs; a rod
    // burns 120 seconds.
    void theRecipesOfTheFire()
    {
        const auto recipe = [this](const QString &name) { return m_content->recipes().at(*m_content->recipeIndex(name)); };
        const QcRecipeDefinition dust = recipe(u"qtcraft:ember_dust"_s);
        QCOMPARE(dust.resultItem, u"qtcraft:ember_dust"_s);
        QCOMPARE(dust.resultCount, 2);
        const QcRecipeDefinition charge = recipe(u"qtcraft:fireball"_s);
        QCOMPARE(charge.resultItem, u"qtcraft:fireball"_s);
        QCOMPARE(charge.resultCount, 3);
        QCOMPARE(charge.ingredients.size(), qsizetype{3});
        QVERIFY(charge.ingredients.contains(u"#qtcraft:coals"_s));
        QCOMPARE(m_content->item(item("qtcraft:ember_rod")).fuelSeconds, 120.0);
    }

    // Used on a block it lights the cell in front and is gone; in creative mode it stays.
    void aFireballLightsOnceInTheHand()
    {
        joinStage();
        clearStage(4, 3);
        hold("qtcraft:fireball", 2);
        const QcBlockPos target = m_feet.offset(1, -1, 1);
        send(QcMsg::UseItem{target, QcBlockFace::Up});
        ticks(2);
        QcTestUtil::pump();
        QCOMPARE(m_server->world().blockAt(target.offset(0, 1, 0)), id("qtcraft:fire"));
        QCOMPARE(player().inventory.slot(player().inventory.selectedHotbarSlot()).count, 1);

        chat(u"/gamemode creative"_s);
        hold("qtcraft:fireball");
        const int before = player().inventory.slot(player().inventory.selectedHotbarSlot()).count;
        QVERIFY(before > 0);
        const QcBlockPos other = m_feet.offset(-1, -1, 1);
        send(QcMsg::UseItem{other, QcBlockFace::Up});
        ticks(2);
        QCOMPARE(m_server->world().blockAt(other.offset(0, 1, 0)), id("qtcraft:fire"));
        QCOMPARE(player().inventory.slot(player().inventory.selectedHotbarSlot()).count, before);
    }

    // A skull stands on a block and looks at the one who set it.
    void aCharredSkullIsPlaced()
    {
        joinStage();
        clearStage(4, 3);
        hold("qtcraft:charred_skull");
        const QcBlockPos target = m_feet.offset(2, -1, 0);
        send(QcMsg::UseItem{target, QcBlockFace::Up});
        ticks(2);
        const std::optional<QcBlockId> placed = m_server->world().blockAt(target.offset(0, 1, 0));
        QVERIFY(placed);
        QCOMPARE(m_content->block(*placed).name, u"qtcraft:charred_skull"_s);
    }

    // A spin travels with the model.
    void aSpinIsPartOfTheContent()
    {
        const QcMobDefinition &wraith = m_content->mobs()[kind(u"ember_wraith"_s)];
        QVERIFY(std::ranges::any_of(wraith.parts, [](const QcMobPart &part) { return part.spin > 0.0; }));
        QVERIFY(std::ranges::any_of(wraith.parts, [](const QcMobPart &part) { return part.spin < 0.0; }));
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcMobSerializer::writeMob(out, wraith);
        }
        QDataStream in(bytes);
        const std::optional<QcMobDefinition> back = QcMobSerializer::readMob(in);
        QVERIFY(back);
        QCOMPARE(back->parts.constLast().spin, wraith.parts.constLast().spin);
    }
};

QTEST_GUILESS_MAIN(TestFortressFolk)
#include "test_fortress_folk.moc"
