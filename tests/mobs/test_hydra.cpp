// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mobs/QcMobStageFixture.h"
#include "server/bosses/QcHydraRules.h"
#include "server/bosses/QcHydraSummoning.h"
#include "server/core/QcServerWorld.h"
#include "server/projectiles/QcProjectileManager.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// The charred hydra (decisions E-1125 to E-1129): its summoning from soul blocks and charred
// skulls, the charge and its blast, the targets it chooses, its skulls, the blocks it breaks after
// a hurt, its armour against arrows, its boss bar, what it leaves and the advancements. Ticks are
// stepped by hand.
class TestHydra : public QcMobStageFixture
{
    Q_OBJECT

private:
    const QcMobDefinition &hydraKind() const { return m_content->mobs()[kind(u"charred_hydra"_s)]; }

    // The stage: a floor of stone 8 blocks to every side, with air above it up to 10.
    void enterArena(bool creative = true)
    {
        joinStage(creative ? u"creative"_s : u"survival"_s);
        floorOf("qtcraft:stone", 8, 10);
    }

    QcCreature *mob(quint32 id) { return m_server->mobs().find(id); }

    // The first hydra in the world, or nullptr.
    QcCreature *firstHydra()
    {
        auto &mobs = m_server->entities().creatures().mobs();
        const auto it = std::ranges::find(mobs, kind(u"charred_hydra"_s), &QcCreature::type);
        return it == mobs.end() ? nullptr : mob(it->id);
    }

    qsizetype hydras() const
    {
        return std::ranges::count(m_server->entities().creatures().mobs(), kind(u"charred_hydra"_s), &QcCreature::type);
    }

    bool done(const char *name)
    {
        const QcAdvancementDefinition *definition = m_content->advancements().find(u"qtcraft:"_s + QString::fromLatin1(name));
        return definition && player().advancements.progress.isDone(*definition);
    }

    // The foot of the T five blocks north of the player.
    QcBlockPos foot() const { return m_feet.offset(0, 0, -5); }

    // A T of `body` along x or z with its foot at foot(), and charred skulls on all of its arms
    // but `last`, the index (0 to 2 along the arms) of the head cell left free; returns that cell.
    QcBlockPos buildT(bool alongX, const char *body, int last)
    {
        const int dx = alongX ? 1 : 0;
        const int dz = alongX ? 0 : 1;
        const QcBlockPos middle = foot().offset(0, 1, 0);
        for (const QcBlockPos cell : {foot(), middle, middle.offset(dx, 0, dz), middle.offset(-dx, 0, -dz)}) {
            m_server->world().setBlock(cell, id(body));
        }
        QcBlockPos free;
        for (int i = 0; i < 3; ++i) {
            const QcBlockPos head = middle.offset((i - 1) * dx, 1, (i - 1) * dz);
            if (i == last) {
                free = head;
            } else {
                m_server->world().setBlock(head, id("qtcraft:charred_skull"));
            }
        }
        return free;
    }

    // The player places a charred skull on the block below `cell`.
    void placeSkull(QcBlockPos cell)
    {
        chat(u"/give charred_skull"_s);
        ticks(1);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:charred_skull"))});
        send(QcMsg::UseItem{cell.offset(0, -1, 0), QcBlockFace::Up, {0.5, 1.0, 0.5}});
        ticks(1);
    }

    // A hydra that fights at once (spawned, not summoned).
    quint32 spawnHydra(double dx, double dz, double dy = 0.0)
    {
        return spawn(u"charred_hydra"_s, dx, dz, dy);
    }

    // Shoots a projectile of `name` by `shooter` from `from` along `velocity`.
    void shoot(const QString &name, quint32 shooter, const QcVec3 &from, const QcVec3 &velocity)
    {
        m_server->entities().projectiles().launch({shooter, name, from, velocity, QcProjectilePickup::Never, 1.0}, m_server->entities().newId());
    }

    // Whether the bar of `hydra` shows on the client: the last message about it says so.
    bool barShown(quint32 hydra) const
    {
        const auto last = std::ranges::find_last(m_client->bossBars, hydra, &QcMsg::BossBar::entity);
        return !last.empty() && last.front().shown;
    }

    QcVec3 middleOf(quint32 id) { return mob(id)->motion.position + QcVec3{0.0, m_content->mobs()[mob(id)->type].height * 0.5, 0.0}; }

private slots:
    // The role model's numbers (decision E-1129).
    void theHydraKeepsItsNumbers()
    {
        const QcMobDefinition &hydra = hydraKind();
        QCOMPARE(hydra.maxHealth, 300.0);
        QCOMPARE(hydra.width, 0.9);
        QCOMPARE(hydra.height, 3.5);
        QCOMPARE(hydra.experience.min, 50);
        QVERIFY(hydra.undead && hydra.fireImmune && hydra.hydra && hydra.bossBar);
        QCOMPARE(hydra.hydra->chargeSeconds, 11.0);
        QCOMPARE(hydra.hydra->blastPower, 7.0);
        QCOMPARE(hydra.hydra->armourShare, 0.5);
        QVERIFY(hydra.immuneTo.contains(u"qtcraft:wither"_s));
        const QcItemDefinition &star = m_content->item(*m_content->itemId(u"qtcraft:nether_star"_s));
        QVERIFY(star.isFireResistant());
    }

    // The last skull on a complete T summons a charging hydra with a third of its health, in either
    // direction, whichever skull comes last, in creative mode too; the seven blocks vanish.
    void theLastSkullSummonsTheHydra_data()
    {
        QTest::addColumn<bool>("alongX");
        QTest::addColumn<int>("last");
        QTest::addColumn<QString>("body");
        QTest::newRow("along x, middle last") << true << 1 << u"qtcraft:soul_sand"_s;
        QTest::newRow("along x, left last") << true << 0 << u"qtcraft:soul_soil"_s;
        QTest::newRow("along z, right last") << false << 2 << u"qtcraft:soul_sand"_s;
    }

    void theLastSkullSummonsTheHydra()
    {
        QFETCH(bool, alongX);
        QFETCH(int, last);
        QFETCH(QString, body);
        enterArena();
        placeSkull(buildT(alongX, body.toUtf8().constData(), last));

        QCOMPARE(hydras(), 1);
        const QcCreature &hydra = m_server->entities().creatures().mobs().front();
        QVERIFY(hydra.persistent);
        QVERIFY(hydra.hydra->charging());
        QCOMPARE(hydra.health, hydraKind().maxHealth / 3.0);
        QCOMPARE(hydra.motion.position.x, foot().x + 0.5);
        QCOMPARE(hydra.motion.position.z, foot().z + 0.5);
        for (int dy = 0; dy <= 2; ++dy) {
            for (int side = -1; side <= 1; ++side) {
                QCOMPARE(m_server->world().blockAt(foot().offset(alongX ? side : 0, dy, alongX ? 0 : side)), qcAirBlockId);
            }
        }
        QVERIFY(done("summon_hydra"));
    }

    // No hydra for a T that is not whole: a stone arm, a block beside the foot, a skull short, a
    // skull on a T of stone.
    void anIncompleteShapeSummonsNothing_data()
    {
        QTest::addColumn<QString>("flaw");
        QTest::newRow("stone arm") << u"arm"_s;
        QTest::newRow("block beside the foot") << u"beside"_s;
        QTest::newRow("a skull short") << u"short"_s;
        QTest::newRow("stone body") << u"stone"_s;
    }

    void anIncompleteShapeSummonsNothing()
    {
        QFETCH(QString, flaw);
        enterArena();
        const QcBlockPos free = buildT(true, flaw == u"stone"_s ? "qtcraft:stone" : "qtcraft:soul_sand", 1);
        if (flaw == u"arm"_s) {
            m_server->world().setBlock(foot().offset(1, 1, 0), id("qtcraft:stone"));
        } else if (flaw == u"beside"_s) {
            m_server->world().setBlock(foot().offset(1, 0, 0), id("qtcraft:soul_sand"));
        } else if (flaw == u"short"_s) {
            m_server->world().setBlock(foot().offset(-1, 2, 0), qcAirBlockId);
        }
        placeSkull(free);
        QVERIFY(!QcHydraSummoning::shapeAt(m_server->world(), free, *hydraKind().hydra));
        QCOMPARE(hydras(), 0);
        QVERIFY(!done("summon_hydra"));
    }

    // Nor does a whole T whose last skull a command sets: only a player's skull summons.
    void aSkullSetByACommandSummonsNothing()
    {
        enterArena();
        const QcBlockPos free = buildT(true, "qtcraft:soul_sand", 1);
        chat(u"/setblock %1 %2 %3 charred_skull"_s.arg(free.x).arg(free.y).arg(free.z));
        ticks(2);
        QCOMPARE(m_server->world().blockAt(free), id("qtcraft:charred_skull"));
        QVERIFY(QcHydraSummoning::shapeAt(m_server->world(), free, *hydraKind().hydra));
        QCOMPARE(hydras(), 0);
    }

    // The charge: no damage, ten health every half second, then a blast of power 7 that breaks
    // blocks under mobGriefing and spares the hydra.
    void theChargeEndsInABlast_data()
    {
        QTest::addColumn<bool>("griefing");
        QTest::newRow("mobGriefing") << true;
        QTest::newRow("no mobGriefing") << false;
    }

    void theChargeEndsInABlast()
    {
        QFETCH(bool, griefing);
        enterArena();
        if (!griefing) {
            chat(u"/gamerule mobGriefing false"_s);
        }
        placeSkull(buildT(true, "qtcraft:soul_sand", 1));
        QCOMPARE(hydras(), 1);
        const quint32 hydra = m_server->entities().creatures().mobs().front().id;
        const QcBlockPos wall = foot().offset(2, 2, 0);
        m_server->world().setBlock(wall, id("qtcraft:stone"));

        QVERIFY(!m_server->mobs().wound(hydra, 50.0));
        QCOMPARE(mob(hydra)->health, 100.0);
        ticks(10);
        QCOMPARE(mob(hydra)->health, 110.0);
        QCOMPARE(mob(hydra)->motion.position.y, static_cast<double>(foot().y));

        ticks(200);
        QVERIFY(mob(hydra)->hydra->charging());
        QCOMPARE(m_server->world().blockAt(wall), id("qtcraft:stone"));
        ticks(20);
        QVERIFY(!mob(hydra)->hydra->charging());
        QCOMPARE(mob(hydra)->health, hydraKind().maxHealth);
        QCOMPARE(m_server->world().blockAt(wall) == qcAirBlockId, griefing);
    }

    // The hydra fights players and every creature that is not undead, and leaves the undead be.
    void itIgnoresTheUndead()
    {
        enterArena();
        const quint32 hydra = spawnHydra(0, -4, 3);
        const quint32 zombie = spawn(u"zombie"_s, 2, -4);
        const quint32 pig = spawn(u"pig"_s, -6, -4);
        mob(zombie)->noAi = true;
        mob(pig)->noAi = true;
        // The pig is kept alive: the skulls would kill it.
        bool aimedAtPig = false;
        for (int tick = 0; tick < 40; ++tick) {
            ticks(1);
            mob(pig)->health = 10.0;
            mob(zombie)->health = 20.0;
            QVERIFY(mob(hydra)->hydra->target != zombie);
            QVERIFY(!std::ranges::contains(mob(hydra)->hydra->sideTargets, zombie));
            aimedAtPig = aimedAtPig || mob(hydra)->hydra->target == pig;
        }
        QVERIFY(aimedAtPig);

        // A creative player is no target; a survival player is.
        chat(u"/gamemode survival"_s);
        m_server->mobs().remove(pig);
        ticks(20);
        QCOMPARE(mob(hydra)->hydra->target, player().id);
    }

    // A skull withers the body it hits for 10 seconds at level II; the hydra's own skulls and
    // those of other undead leave it unhurt.
    void theSkullWithers()
    {
        enterArena();
        const quint32 pig = spawn(u"pig"_s, 0, -4);
        mob(pig)->noAi = true;
        mob(pig)->health = 100.0;
        shoot(u"qtcraft:hydra_skull"_s, 0, middleOf(pig) + QcVec3{0.0, 0.0, 2.0}, {0.0, 0.0, -16.0});
        ticks(3);
        QVERIFY(mob(pig));
        QVERIFY(mob(pig)->effects);
        const QcStatusEffect *withering = mob(pig)->effects->find(u"qtcraft:wither"_s);
        QVERIFY(withering);
        QCOMPARE(withering->level, 2);
        QVERIFY(withering->ticksLeft > 180 && withering->ticksLeft <= 200);

        const quint32 shooter = spawnHydra(6, -8, 3);
        const quint32 target = spawnHydra(0, -8, 3);
        mob(shooter)->noAi = true;
        mob(target)->noAi = true;
        shoot(u"qtcraft:hydra_skull"_s, shooter, middleOf(target) + QcVec3{0.0, 0.0, 2.0}, {0.0, 0.0, -16.0});
        ticks(3);
        QCOMPARE(mob(target)->health, hydraKind().maxHealth);
        QCOMPARE(mob(shooter)->health, hydraKind().maxHealth);
    }

    // A charged skull breaks obsidian and leaves bedrock; a black one leaves obsidian.
    void theChargedSkullBreaksHardBlocks_data()
    {
        QTest::addColumn<QString>("skull");
        QTest::addColumn<QString>("block");
        QTest::addColumn<bool>("breaks");
        QTest::newRow("charged on obsidian") << u"qtcraft:charged_hydra_skull"_s << u"qtcraft:obsidian"_s << true;
        QTest::newRow("charged on bedrock") << u"qtcraft:charged_hydra_skull"_s << u"qtcraft:bedrock"_s << false;
        QTest::newRow("black on obsidian") << u"qtcraft:hydra_skull"_s << u"qtcraft:obsidian"_s << false;
    }

    void theChargedSkullBreaksHardBlocks()
    {
        QFETCH(QString, skull);
        QFETCH(QString, block);
        QFETCH(bool, breaks);
        enterArena();
        const QcBlockPos target = m_feet.offset(0, 1, -5);
        m_server->world().setBlock(target, id(block.toUtf8().constData()));
        shoot(skull, 0, {target.x + 0.5, target.y + 0.5, target.z + 2.5}, {0.0, 0.0, -5.0});
        ticks(20);
        QCOMPARE(m_server->world().blockAt(target) == qcAirBlockId, breaks);
    }

    // A moment after a fresh hurt it breaks the blocks around it, under mobGriefing only.
    void aHurtBreaksTheBlocksAround_data()
    {
        QTest::addColumn<bool>("griefing");
        QTest::newRow("mobGriefing") << true;
        QTest::newRow("no mobGriefing") << false;
    }

    void aHurtBreaksTheBlocksAround()
    {
        QFETCH(bool, griefing);
        enterArena();
        if (!griefing) {
            chat(u"/gamerule mobGriefing false"_s);
        }
        const quint32 hydra = spawnHydra(0, -5);
        const QcBlockPos beside = foot().offset(1, 1, 0);
        const QcBlockPos bedrock = foot().offset(-1, 1, 0);
        m_server->world().setBlock(beside, id("qtcraft:stone"));
        m_server->world().setBlock(bedrock, id("qtcraft:bedrock"));
        QVERIFY(!m_server->mobs().wound(hydra, 1.0));
        ticks(QcHydraState::breakDelayTicks - 2);
        QCOMPARE(m_server->world().blockAt(beside), id("qtcraft:stone"));
        // It may have moved a little: the cells around where it stands now.
        mob(hydra)->motion.position = {foot().x + 0.5, static_cast<double>(foot().y), foot().z + 0.5};
        ticks(3);
        QCOMPARE(m_server->world().blockAt(beside) == qcAirBlockId, griefing);
        QCOMPARE(m_server->world().blockAt(bedrock), id("qtcraft:bedrock"));
    }

    // Below half its health arrows glance off; above it they hurt.
    void itsArmourStopsArrows_data()
    {
        QTest::addColumn<double>("health");
        QTest::addColumn<bool>("hurts");
        QTest::newRow("above half") << 200.0 << true;
        QTest::newRow("below half") << 140.0 << false;
    }

    void itsArmourStopsArrows()
    {
        QFETCH(double, health);
        QFETCH(bool, hurts);
        enterArena();
        const quint32 hydra = spawnHydra(0, -5);
        mob(hydra)->noAi = true;
        mob(hydra)->health = health;
        shoot(u"qtcraft:arrow"_s, 0, middleOf(hydra) + QcVec3{0.0, 0.0, 2.0}, {0.0, 0.0, -25.0});
        ticks(3);
        QCOMPARE(mob(hydra)->health < health, hurts);
    }

    // Players near it see its boss bar; a showcase's display (without a mind or in its pen) has none.
    void itShowsABossBar()
    {
        enterArena();
        const quint32 hydra = spawnHydra(0, -5);
        ticks(2);
        QVERIFY(player().view.bossBars.contains(hydra));
        QVERIFY(barShown(hydra));
        const QcMsg::BossBar bar = m_client->bossBars.constLast();
        QCOMPARE(bar.kind, u"qtcraft:charred_hydra"_s);
        QCOMPARE(bar.colour, static_cast<quint8>(QcBossBarColour::Purple));
        QCOMPARE(bar.progress, 1.0f);

        // A change of its health is told; nothing else is told again.
        mob(hydra)->health = 150.0;
        ticks(2);
        QCOMPARE(m_client->bossBars.constLast().progress, 0.5f);
        const qsizetype told = m_client->bossBars.size();
        ticks(5);
        QCOMPARE(m_client->bossBars.size(), told);

        mob(hydra)->noAi = true;
        ticks(3);
        QVERIFY(!player().view.bossBars.contains(hydra));
        QVERIFY(!barShown(hydra));

        // One in its pen (the showcase's creature gallery) has none either.
        mob(hydra)->noAi = false;
        ticks(2);
        QVERIFY(barShown(hydra));
        mob(hydra)->confined = true;
        ticks(3);
        QVERIFY(!barShown(hydra));
    }

    // The bar goes when the player leaves its range of 160 m, changes dimension (the client clears
    // its bars on QcMsg::DimensionChange, test_boss_bars) and when the hydra dies.
    void theBossBarGoes()
    {
        enterArena();
        // Kept like a summoned one, so its chunk takes it along while the player is away.
        const QcVec3 at{m_feet.x + 0.5, static_cast<double>(m_feet.y), m_feet.z - 4.5};
        const quint32 hydra = m_server->mobs().spawn(kind(u"charred_hydra"_s), at, m_server->entities().newId(), false, true);
        ticks(2);
        QVERIFY(barShown(hydra));

        // Out of range (170 m away its chunk is put away as well); back, it shows again.
        player().motion.position.z += 170.0;
        ticks(2);
        QVERIFY(!barShown(hydra));
        player().motion.position.z -= 170.0;
        loadAround(m_feet, 2);
        for (int tick = 0; tick < 40 && !firstHydra(); ++tick) {
            ticks(1);
        }
        QVERIFY(firstHydra());
        const quint32 again = firstHydra()->id;
        ticks(2);
        QVERIFY(barShown(again));

        // In the nether nothing of it is told; back, it is told anew.
        const qsizetype changes = m_client->dimensions.size();
        chat(u"/tp 0.5 120 0.5 in qtcraft:nether"_s);
        ticks(3);
        QCOMPARE(m_client->dimensions.size(), changes + 1);
        const qsizetype told = m_client->bossBars.size();
        ticks(3);
        QCOMPARE(m_client->bossBars.size(), told);
        chat(u"/tp %1 %2 %3 in qtcraft:overworld"_s.arg(m_feet.x + 0.5).arg(m_feet.y).arg(m_feet.z + 0.5));
        ticks(3);
        QCOMPARE(m_client->dimensions.size(), changes + 2);
        QVERIFY(firstHydra());
        QVERIFY(m_client->bossBars.size() > told);
        QVERIFY(barShown(firstHydra()->id));
        const quint32 back = firstHydra()->id;

        QVERIFY(m_server->mobs().wound(back, 1000.0));
        ticks(3);
        QVERIFY(!barShown(back));
    }

    // A hydra saved while it charges goes on charging when the world is loaded again: still
    // unhurt, and the blast still comes (the rest of its state starts afresh).
    void theChargeSurvivesASave()
    {
        QTemporaryDir dir;
        join(configIn(dir.path()));
        loadAround(m_feet, 2);
        chat(u"/gamemode creative"_s);
        m_server->mobs().setNaturalSpawning(false);
        m_server->mobs().removeAll();
        floorOf("qtcraft:stone", 8, 10);
        placeSkull(buildT(true, "qtcraft:soul_sand", 1));
        QCOMPARE(hydras(), 1);
        ticks(50);
        QVERIFY(firstHydra() && firstHydra()->hydra);
        const int left = firstHydra()->hydra->chargeTicks;
        QVERIFY(left > 100);
        QVERIFY(m_server->save().has_value());

        reopen(dir.path());
        loadAround(m_feet, 2);
        ticks(1);
        QCOMPARE(hydras(), 1);
        QVERIFY(firstHydra() && firstHydra()->hydra);
        const quint32 hydra = firstHydra()->id;
        QVERIFY(mob(hydra)->hydra->charging());
        QVERIFY(mob(hydra)->hydra->chargeTicks <= left);
        QVERIFY(!m_server->mobs().wound(hydra, 50.0));
        const QcBlockPos wall = foot().offset(2, 2, 0);
        m_server->world().setBlock(wall, id("qtcraft:stone"));
        ticks(left + 5);
        QVERIFY(!mob(hydra)->hydra->charging());
        QCOMPARE(m_server->world().blockAt(wall), qcAirBlockId);
    }

    // (d) What the client is told to draw: the armour all through the charge but for its last
    // ticks, where it blinks every 5 ticks, and after it at or below half its health.
    void theArmourShows_data()
    {
        QTest::addColumn<int>("charge");
        QTest::addColumn<double>("health");
        QTest::addColumn<bool>("shows");
        QTest::newRow("charging") << 200 << 100.0 << true;
        QTest::newRow("blinking on") << 30 << 100.0 << true;
        QTest::newRow("blinking off") << 25 << 100.0 << false;
        QTest::newRow("full health") << 0 << 300.0 << false;
        QTest::newRow("just above half") << 0 << 150.5 << false;
        QTest::newRow("half") << 0 << 150.0 << true;
        QTest::newRow("low") << 0 << 10.0 << true;
    }

    void theArmourShows()
    {
        QFETCH(int, charge);
        QFETCH(double, health);
        QFETCH(bool, shows);
        enterArena();
        const quint32 hydra = spawnHydra(0, -5);
        mob(hydra)->hydra->chargeTicks = charge;
        mob(hydra)->health = health;
        QCOMPARE(QcHydraRules::showsArmour(*mob(hydra), hydraKind()), shows);
    }

    // Killed by a player it leaves a nether star and experience, and he gets the advancement.
    void itLeavesANetherStar()
    {
        enterArena(false);
        const quint32 hydra = spawnHydra(1.5, 0);
        mob(hydra)->noAi = true;
        mob(hydra)->health = 1.0;
        player().attackCooldown = 0.0;
        send(QcMsg::AttackEntity{hydra});
        ticks(3);
        QVERIFY(!mob(hydra));
        QCOMPARE(droppedCount(item("qtcraft:nether_star")), 1);
        QVERIFY(m_server->orbs().totalPoints() >= 50);
        QVERIFY(done("kill_hydra"));
    }
};

QTEST_GUILESS_MAIN(TestHydra)
#include "test_hydra.moc"
