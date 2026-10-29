// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcMachineFixture.h"
#include "fixtures/QcTestUtil.h"

#include <QSet>

using namespace Qt::StringLiterals;

// What lights fire and what fire does to bodies (stage 34.2, decision E-393): flint and steel lights
// the face it is used on where fire may stand and wears; a dispenser uses it and keeps it when
// nothing catches; lava lights only cells right next to it that touch a flammable block, with its
// small chance and the same way for the same seed; players and creatures in fire burn for a time,
// take damage every second and water puts them out; punching fire puts it out; the hay bale; and
// /summon of minecarts and the minecart phase of a world without carts. Ticks are stepped by hand.
class TestFireSources : public QcMachineFixture
{
    Q_OBJECT

private:
    void set(QcBlockPos pos, const char *block) { QVERIFY(m_server->world().setBlock(pos, id(block))); }
    void clear(QcBlockPos pos) { QVERIFY(m_server->world().setBlock(pos, qcAirBlockId)); }
    bool isAir(QcBlockPos pos) const { return m_server->world().blockAt(pos) == qcAirBlockId; }

    bool isFire(QcBlockPos pos) const { return m_server->world().blockAt(pos) == id("qtcraft:fire"); }

    // Gives the raw player one flint and steel and selects it.
    void holdFlintAndSteel()
    {
        chat(u"/give flint_and_steel 1"_s);
        ticks(1);
        const int slot = slotOf(item("qtcraft:flint_and_steel"));
        QVERIFY(slot >= 0 && slot < QcInventory::hotbarSize);
        send(QcMsg::SelectHotbarSlot{slot});
    }

    // Wear of the flint and steel the player holds; -1 when it is gone.
    int heldWear() const
    {
        const int slot = slotOf(item("qtcraft:flint_and_steel"));
        return slot < 0 ? -1 : m_client->stacks[slot].damage;
    }

    void strike(QcBlockPos target, QcBlockFace face) { send(QcMsg::UseItem{target, face}); }

    // Lava's block updates: every tick the block north of it changes by command (stone and
    // cobblestone in turn), `rounds` times. Every fire that appears near the lava is noted with its
    // round and put out again, so each update has its chance. Returns the cells and the rounds.
    QList<std::pair<int, QcBlockPos>> stirLava(QcBlockPos lava, int rounds)
    {
        QList<std::pair<int, QcBlockPos>> lit;
        const QcBlockPos stirred = lava.offset(0, 0, -1);
        for (int round = 0; round < rounds; ++round) {
            chat(u"/setblock %1 %2 %3 %4"_s.arg(stirred.x).arg(stirred.y).arg(stirred.z).arg(round % 2 == 0 ? u"cobblestone"_s : u"stone"_s));
            ticks(1);
            for (int dx = -3; dx <= 3; ++dx) {
                for (int dy = -1; dy <= 3; ++dy) {
                    for (int dz = -3; dz <= 3; ++dz) {
                        if (isFire(lava.offset(dx, dy, dz))) {
                            lit.append({round, lava.offset(dx, dy, dz)});
                            clear(lava.offset(dx, dy, dz));
                        }
                    }
                }
            }
        }
        return lit;
    }

    // A lava source in the middle of the build area, walled in by stone on every side (it cannot
    // flow), with an oak log in its west wall and planks one above and east of it: of the cells right
    // next to the lava only the air above it touches a flammable block. Fire spreads (lava lights
    // nothing otherwise), but every fire is put out in the tick it appears, long before its first
    // step, so only the lava lights anything. `planksAway` moves the planks further east, out of
    // that reach.
    QcBlockPos lavaBesidePlanks(int planksAway = 1, QcServerConfig config = {})
    {
        // The local player's commands are not rate limited: one /setblock every tick.
        QcServerTestFixture::m_origin = QcServer::ClientOrigin::Local;
        buildArea(std::move(config));
        const QcBlockPos lava = at(6, 0, 6);
        set(lava, "qtcraft:lava");
        for (const QcBlockFace face : {QcBlockFace::North, QcBlockFace::South, QcBlockFace::East}) {
            set(qcNeighbour(lava, face), "qtcraft:stone");
        }
        set(lava.offset(-1, 0, 0), "qtcraft:oak_log");
        set(lava.offset(planksAway, 1, 0), "qtcraft:oak_planks");
        ticks(1);
        return lava;
    }

    // A creature of `name` standing at `feet`; returns its id.
    quint32 creature(const char *name, QcBlockPos feet)
    {
        const std::optional<int> type = m_content->mobIndex(QString::fromLatin1(name));
        if (!type) {
            QcTestUtil::failCase(QStringLiteral("Test uses the creature %1, which the content does not have").arg(QString::fromLatin1(name)));
            return 0;
        }
        return m_server->mobs().spawn(*type, {feet.x + 0.5, static_cast<double>(feet.y), feet.z + 0.5}, m_server->entities().newId(), false, true);
    }

private slots:
    void init() { QcServerTestFixture::m_origin = QcServer::ClientOrigin::Remote; }

    void flintAndSteelLightsTheClickedFace()
    {
        join();
        holdFlintAndSteel();
        QCOMPARE(heldWear(), 0);

        // The top of the grass: fire on the ground in front of the face, one use worn off.
        strike(m_feet.offset(2, -1, 0), QcBlockFace::Up);
        QVERIFY(isFire(m_feet.offset(2, 0, 0)));
        QCOMPARE(heldWear(), 1);
        QCOMPARE(m_server->systems().fire.numbers().lit, 1u);

        // The side of a stone block: the cell west of it, on the grass.
        set(m_feet.offset(-1, 0, 2), "qtcraft:stone");
        strike(m_feet.offset(-1, 0, 2), QcBlockFace::West);
        QVERIFY(isFire(m_feet.offset(-2, 0, 2)));
        QCOMPARE(heldWear(), 2);

        // The side of planks hanging in the air: fire may stand next to them although nothing is below.
        set(m_feet.offset(-2, 2, -2), "qtcraft:oak_planks");
        strike(m_feet.offset(-2, 2, -2), QcBlockFace::East);
        QVERIFY(isFire(m_feet.offset(-1, 2, -2)));
        QCOMPARE(heldWear(), 3);

        // The side of stone hanging in the air: nothing to stand on, nothing lit, nothing worn.
        set(m_feet.offset(2, 2, 2), "qtcraft:stone");
        strike(m_feet.offset(2, 2, 2), QcBlockFace::West);
        QVERIFY(isAir(m_feet.offset(1, 2, 2)));
        QCOMPARE(heldWear(), 3);
        QCOMPARE(m_server->systems().fire.numbers().lit, 3u);

        // Fire is a spark of the queue, never an orphan waiting for adoption.
        QCOMPARE(m_server->fires().unplannedCount(), 0);
        QVERIFY(m_server->fires().contains(m_feet.offset(2, 0, 0)));
    }

    void flintAndSteelWearsOutAndCreativeKeepsIt()
    {
        join();
        holdFlintAndSteel();
        const int durability = m_content->item(item("qtcraft:flint_and_steel")).durability;
        QCOMPARE(durability, 64);

        // One use left: the next fire breaks it.
        for (int i = 0; i < durability - 1; ++i) {
            strike(m_feet.offset(2, -1, 0), QcBlockFace::Up);
            QVERIFY(isFire(m_feet.offset(2, 0, 0)));
            clear(m_feet.offset(2, 0, 0));
            ticks(1);
        }
        QCOMPARE(heldWear(), durability - 1);
        strike(m_feet.offset(2, -1, 0), QcBlockFace::Up);
        QVERIFY(isFire(m_feet.offset(2, 0, 0)));
        QCOMPARE(heldWear(), -1);

        // In creative mode it does not wear.
        chat(u"/gamemode creative"_s);
        holdFlintAndSteel();
        strike(m_feet.offset(-2, -1, 0), QcBlockFace::Up);
        QVERIFY(isFire(m_feet.offset(-2, 0, 0)));
        QCOMPARE(heldWear(), 0);
    }

    void punchingOrBreakingFirePutsItOut()
    {
        join();
        holdFlintAndSteel();
        strike(m_feet.offset(2, -1, 0), QcBlockFace::Up);
        const QcBlockPos fire = m_feet.offset(2, 0, 0);
        QVERIFY(isFire(fire));

        // Survival: fire breaks at the first strike and drops nothing.
        send(QcMsg::StartMining{fire});
        send(QcMsg::FinishMining{fire});
        QVERIFY(isAir(fire));

        // Creative: the same.
        chat(u"/gamemode creative"_s);
        holdFlintAndSteel();
        strike(m_feet.offset(2, -1, 0), QcBlockFace::Up);
        QVERIFY(isFire(fire));
        send(QcMsg::StartMining{fire});
        send(QcMsg::FinishMining{fire});
        QVERIFY(isAir(fire));
    }

    void dispenserLightsInFrontAndKeepsFlintAndSteel()
    {
        buildArea();
        put(4, 0, 5, u"signal_dispenser[facing=east]"_s);
        put(4, 0, 6, u"lever[attach=floor]"_s);
        fill(4, 0, 5, {stack("qtcraft:flint_and_steel", 1)});
        ticks(2);

        flip(4, 0, 6);
        ticks(6);
        QVERIFY(isFire(at(5, 0, 5)));
        QCOMPARE(slotsAt(4, 0, 5).value(0).item, item("qtcraft:flint_and_steel"));
        QCOMPARE(slotsAt(4, 0, 5).value(0).damage, 1);
        flip(4, 0, 6);
        ticks(2);

        // The cell in front burns already: nothing is lit, the flint and steel stays unworn inside.
        flip(4, 0, 6);
        ticks(6);
        QCOMPARE(slotsAt(4, 0, 5).value(0).item, item("qtcraft:flint_and_steel"));
        QCOMPARE(slotsAt(4, 0, 5).value(0).damage, 1);
        QCOMPARE(everywhere("qtcraft:flint_and_steel"), 1);
    }

    void lavaLightsOnlyRightNextToItAndRarely()
    {
        const QcBlockPos lava = lavaBesidePlanks();
        constexpr int rounds = 400;
        const QList<std::pair<int, QcBlockPos>> lit = stirLava(lava, rounds);

        // Only the air above the lava touches a flammable block (the planks east of it); the log in
        // the west wall has no free cell next to the lava. With 3 in 100 per update some updates
        // light it, far from all; every fire is a spark through QcServerFire::ignite.
        QVERIFY2(lit.size() >= 2 && lit.size() <= rounds / 10, qPrintable(QString::number(lit.size())));
        for (const auto &[round, cell] : lit) {
            QCOMPARE(cell, lava.offset(0, 1, 0));
        }
        QCOMPARE(m_server->systems().fire.numbers().lit, quint64(lit.size()));
        QCOMPARE(m_server->world().blockAt(lava.offset(1, 1, 0)), id("qtcraft:oak_planks"));
        QCOMPARE(m_server->world().blockAt(lava.offset(-1, 0, 0)), id("qtcraft:oak_log"));
    }

    void lavaLightsTheSameForTheSameSeed()
    {
        QList<QList<std::pair<int, QcBlockPos>>> runs;
        for (int run = 0; run < 2; ++run) {
            runs.append(stirLava(lavaBesidePlanks(), 200));
        }
        QVERIFY(!runs[0].isEmpty());
        QCOMPARE(runs[0], runs[1]);
    }

    void lavaWithoutFuelNextToItLightsNothing()
    {
        // The planks two cells east of the air above the lava: no cell right next to the lava
        // touches them.
        stirLava(lavaBesidePlanks(2), 200);
        QCOMPARE(m_server->systems().fire.numbers().lit, 0u);
    }

    // Without fire spread (the game rule or a fireSpreadRadius of 0) lava lights nothing.
    void lavaLightsNothingWithoutFireSpread()
    {
        for (const bool byRule : {true, false}) {
            QcServerConfig config;
            if (!byRule) {
                config.limits.fireSpreadRadius = 0;
            }
            const QcBlockPos lava = lavaBesidePlanks(1, config);
            if (byRule) {
                chat(u"/gamerule fireSpread false"_s);
                ticks(1);
            }
            QVERIFY(!m_server->systems().fire.spreads());
            QVERIFY(stirLava(lava, 400).isEmpty());
            QCOMPARE(m_server->systems().fire.numbers().lit, 0u);
        }
    }

    void playerInFireBurnsAndWaterPutsItOut()
    {
        join();
        set(m_feet, "qtcraft:fire");
        idle(1);
        QVERIFY(m_client->burning);
        clear(m_feet);
        idle(1);
        QVERIFY(m_client->burning);

        // A burning player loses health every second while out of the fire.
        const double before = m_client->health;
        idle(3 * QcBodyBurning::burnIntervalTicks);
        QVERIFY(m_client->burning);
        QVERIFY2(m_client->health <= before - 2.0 && m_client->health >= before - 3.0 * QcBodyBurning::burnDamage,
                 qPrintable(QString::number(before - m_client->health)));

        set(m_feet, "qtcraft:water");
        idle(1);
        QVERIFY(!m_client->burning);
        const double wet = m_client->health;
        idle(2 * QcBodyBurning::burnIntervalTicks);
        QVERIFY(m_client->health >= wet);
    }

    void burningLastsTheBlocksTime()
    {
        join();
        set(m_feet, "qtcraft:fire");
        idle(1);
        clear(m_feet);

        // Fire sets a body alight for 8 s (setsAlight of the content).
        const int alight = m_content->block(id("qtcraft:fire")).fire.setsAlightSeconds * static_cast<int>(QcPhysics::tickRate);
        QCOMPARE(alight, 160);
        int burnt = 0;
        while (m_client->burning && burnt < 2 * alight) {
            idle(1);
            ++burnt;
        }
        QVERIFY2(std::abs(burnt - alight) <= 1, qPrintable(QString::number(burnt)));
    }

    void creativePlayersDoNotBurn()
    {
        join();
        chat(u"/gamemode creative"_s);
        ticks(1);
        set(m_feet, "qtcraft:fire");
        idle(5);
        QVERIFY(!m_client->burning);
    }

    void burningCreatureIsHurtAndSeenBurning()
    {
        buildArea();
        chat(u"/gamerule fireSpread false"_s);
        const QcBlockPos feet = at(6, 0, 6);
        const quint32 pig = creature("qtcraft:pig", feet);
        set(feet, "qtcraft:fire");
        ticks(1);
        QVERIFY(m_server->mobs().find(pig)->burnTicks > 0);
        clear(feet);

        const double before = m_server->mobs().find(pig)->health;
        ticks(2 * QcBodyBurning::burnIntervalTicks + 1);
        QVERIFY2(m_server->mobs().find(pig)->health <= before - 2.0 * QcBodyBurning::burnDamage,
                 qPrintable(QString::number(before - m_server->mobs().find(pig)->health)));

        // Water around the creature puts it out.
        const QcBlockPos cell = QcPhysics::blockContaining(m_server->mobs().find(pig)->motion.position);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                set(cell.offset(dx, 0, dz), "qtcraft:water");
            }
        }
        ticks(1);
        QCOMPARE(m_server->mobs().find(pig)->burnTicks, 0);
    }

    // A burning creature makes its hurt sound when the damage takes, and dies with its loot.
    void burningCreatureDiesWithItsLoot()
    {
        buildArea();
        chat(u"/gamerule fireSpread false"_s);
        const QcBlockPos feet = at(6, 0, 6);
        const quint32 pig = creature("qtcraft:pig", feet);
        const QcMobDefinition &definition = m_content->mobs()[m_server->mobs().find(pig)->type];
        QVERIFY(!definition.hurtSound.isEmpty() && !definition.drops.isEmpty());
        set(feet, "qtcraft:fire");
        ticks(1);
        clear(feet);
        m_client->sounds.clear();

        ticks(QcBodyBurning::burnIntervalTicks + 1);
        QVERIFY(m_server->mobs().find(pig));
        QVERIFY2(m_client->sounds.contains(definition.hurtSound), qPrintable(m_client->sounds.join(u' ')));

        m_server->mobs().find(pig)->health = QcBodyBurning::burnDamage;
        ticks(QcBodyBurning::burnIntervalTicks + 1);
        QVERIFY(!m_server->mobs().find(pig));
        QVERIFY(m_client->sounds.contains(definition.deathSound));
        const std::optional<QcItemId> loot = m_content->itemId(definition.drops.constFirst().item);
        QVERIFY(loot);
        QCOMPARE(droppedCount(*loot), definition.drops.constFirst().count);
    }

    // Flint and steel on magma lights an eternal fire: it needs no room in the caps and keeps none,
    // so even a world full of fires takes it; when its ground stops being eternal it needs room
    // again, and without room it goes out.
    void eternalFireStaysOutOfTheCaps()
    {
        QcServerConfig config;
        config.limits.fires = 1;
        join(config);
        holdFlintAndSteel();
        QVERIFY(m_server->systems().fire.ignite(m_feet.offset(2, 0, 0)));
        QVERIFY(!m_server->fires().hasRoom(m_feet.chunk()));

        const QcBlockPos magma = m_feet.offset(-2, -1, 0);
        set(magma, "qtcraft:magma_block");
        strike(magma, QcBlockFace::Up);
        const QcBlockPos eternal = magma.offset(0, 1, 0);
        QVERIFY(isFire(eternal));
        QVERIFY(m_server->fires().entry(eternal)->eternal);
        QCOMPARE(m_server->fires().size(), 2);
        QCOMPARE(m_server->fires().counted(), 1);
        QCOMPARE(m_server->systems().fire.numbers().refused, 0u);
        QVERIFY(!m_server->systems().fire.ignite(m_feet.offset(0, 0, 2)));

        // Its ground turns to stone while the world is full: the fire goes out, not adopted anew.
        set(magma, "qtcraft:stone");
        ticks(1);
        QVERIFY(isAir(eternal));
        QCOMPARE(m_server->fires().size(), 1);

        // With room the same fire stays and counts again.
        set(magma, "qtcraft:magma_block");
        clear(m_feet.offset(2, 0, 0));
        strike(magma, QcBlockFace::Up);
        QVERIFY(isFire(eternal));
        QCOMPARE(m_server->fires().counted(), 0);
        set(magma, "qtcraft:stone");
        ticks(1);
        QVERIFY(isFire(eternal));
        QVERIFY(!m_server->fires().entry(eternal)->eternal);
        QCOMPARE(m_server->fires().counted(), 1);
    }

    void burningShowsOnTheEntity()
    {
        join();
        const quint32 pig = creature("qtcraft:pig", m_feet.offset(2, 0, 2));
        m_server->mobs().find(pig)->burnTicks = 100;
        ticks(2);
        QVERIFY((m_client->flags.value(pig) & QcMsg::Burning) != 0);
    }

    void itemsBurnUpInFire()
    {
        join();
        set(m_feet.offset(2, 0, 0), "qtcraft:fire");
        QcItemEntityManager::Drop drop;
        drop.stack = {item("qtcraft:stick"), 1, 0};
        drop.position = {m_feet.x + 2.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        m_server->items().spawn(drop, m_server->entities().newId());
        ticks(2);
        QCOMPARE(droppedCount(item("qtcraft:stick")), 0);
    }

    void hayBale()
    {
        const QcBlockDefinition &hay = m_content->block(id("qtcraft:hay_block"));
        QVERIFY(hay.fire.isFlammable());
        QCOMPARE(hay.fire.ignite, 60);
        bool made = false;
        bool undone = false;
        for (const QcRecipeDefinition &recipe : m_content->recipes()) {
            if (recipe.resultItem == u"qtcraft:hay_block"_s) {
                made = recipe.width() == 3 && recipe.height() == 3 && recipe.itemAt(1, 1) == u"qtcraft:wheat"_s;
            }
            if (recipe.resultItem == u"qtcraft:wheat"_s && recipe.ingredients == QList<QString>{u"qtcraft:hay_block"_s}) {
                undone = recipe.resultCount == 9;
            }
        }
        QVERIFY(made);
        QVERIFY(undone);
    }

    void summonMinecarts()
    {
        join();
        // Three metres ahead (yaw 0 looks north): a rail there, the first cart stands on it.
        const QcBlockPos rail = m_feet.offset(0, 0, -3);
        set(rail, "qtcraft:rail");
        chat(u"/summon minecart"_s);
        ticks(1);
        QCOMPARE(m_server->carts().all().size(), 1);
        QCOMPARE(m_server->carts().all().first().motion.position, *m_server->systems().minecarts.restingPlace(rail));

        chat(u"/summon chest_minecart 2"_s);
        chat(u"/summon qtcraft:hopper_minecart"_s);
        ticks(1);
        QCOMPARE(m_server->carts().all().size(), 4);
        int containers = 0;
        for (const QcMinecart &cart : m_server->carts().all()) {
            containers += cart.stacks.isEmpty() ? 0 : 1;
        }
        QCOMPARE(containers, 3);
        QVERIFY(m_client->chatLines.last().contains(u"hopper_minecart"_s));
    }

    void minecartPhaseWithoutCartsForgetsTheLastTick()
    {
        join();
        // A hopper cart on a rail picks up an item lying in its cell.
        const QcBlockPos rail = m_feet.offset(2, 0, 2);
        set(rail, "qtcraft:rail");
        QcMinecart *cart = m_server->systems().minecarts.summon(u"qtcraft:hopper_minecart"_s, {rail.x + 0.5, static_cast<double>(rail.y), rail.z + 0.5}, 0.0f);
        QVERIFY(cart);
        const quint32 id = cart->id;
        QcItemEntityManager::Drop drop;
        drop.stack = {item("qtcraft:stick"), 1, 0};
        drop.position = {rail.x + 0.5, rail.y + 0.2, rail.z + 0.5};
        drop.pickupDelay = 0.0;
        m_server->items().spawn(drop, m_server->entities().newId());
        for (int tick = 0; tick < 40 && m_server->systems().minecarts.hoppers().lastTick().moved == 0; ++tick) {
            ticks(1);
        }
        QCOMPARE(m_server->systems().minecarts.hoppers().lastTick().moved, 1);
        QCOMPARE(m_server->systems().minecarts.hoppers().lastTick().acting, 1);

        // The cart goes: the next tick reports nothing acting, as a tick over no carts would.
        QVERIFY(m_server->carts().remove(id).has_value());
        ticks(1);
        QCOMPARE(m_server->systems().minecarts.hoppers().lastTick().acting, 0);
        QCOMPARE(m_server->systems().minecarts.hoppers().lastTick().moved, 0);
        QVERIFY(m_server->carts().activated().isEmpty());
    }
};

QTEST_MAIN(TestFireSources)
#include "test_fire_sources.moc"
