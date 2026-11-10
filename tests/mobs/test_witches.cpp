// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/items/QcProjectileParser.h"
#include "common/content/registry/QcContentLoot.h"
#include "common/content/registry/QcContentParser.h"
#include "common/diagnostics/QcThreadCpuClock.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcDayCycle.h"
#include "fixtures/QcServerTestFixture.h"
#include "server/gameplay/QcServerEffects.h"
#include "server/mobbrains/QcArcherBrain.h"
#include "server/mobbrains/QcArcherState.h"

#include <QJsonDocument>
#include <QScopeGuard>
#include <QTest>

using namespace Qt::StringLiterals;

// Witches (decisions E-623, E-1105, E-1106) and splash potions (decision E-624): a witch is an
// archer from data that chooses its potion by the target (slowness from afar, poison while healthy,
// weakness up close by chance, else harming), drinks water breathing, fire resistance, healing or
// swiftness when it needs them and stands still meanwhile, has no blows and leaves draws of its
// loot table; a splash reaches every living body within its radius but the thrower, weaker with
// the distance, harming through armour. Ticks are stepped by hand.
class TestWitches : public QcServerTestFixture
{
    Q_OBJECT

private:
    static constexpr double midnight = QcDayCycle::dayLength * 10.75;
    static constexpr int arena = 16;
    // Ticks a drink with a chance of 5 % per tick begins within, but for a chance of about 1e-9.
    static constexpr int patience = 400;

    // Projectiles nextShot already returned.
    QSet<quint32> m_seenShots;

    QcServerClient &player() { return *m_server->entities().clients().front(); }
    QcItemStack stack(const char *name, int count) const { return {item(name), count, 0}; }
    QcCreatureKindId type(const char *name) const { return *m_content->mobKind(u"qtcraft:"_s + QString::fromLatin1(name)); }
    QcCreature *mob(quint32 id) { return m_server->entities().creatures().find(id); }
    const QcMobDefinition &witchKind() const { return m_content->mobs()[type("witch")]; }
    int drinkTicks() const { return static_cast<int>(std::lround(witchKind().drinks->seconds / QcPhysics::tickDuration)); }

    // A flat, open arena of grass around the player, at night, no natural spawning.
    void joinArena()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        chat(u"/gamerule daylightCycle false"_s);
        m_server->setWorldTime(midnight);
        loadAround(m_feet, 2);
        for (int dx = -arena; dx <= arena; ++dx) {
            for (int dz = -arena; dz <= arena; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:grass_block"));
                for (int dy = 0; dy <= 8; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
    }

    quint32 spawn(const char *name, double dx, double dz)
    {
        const QcVec3 at{m_feet.x + 0.5 + dx, static_cast<double>(m_feet.y), m_feet.z + 0.5 + dz};
        return m_server->entities().creatures().spawn(type(name), at, m_server->entities().newId());
    }

    // The first projectile of `shooter` within `count` ticks that no earlier call returned, if any.
    std::optional<QcProjectile> nextShot(quint32 shooter, int count)
    {
        for (int tick = 0; tick < count; ++tick) {
            ticks(1);
            for (const QcProjectile &projectile : m_server->entities().projectiles().all()) {
                if (projectile.shooter == shooter && !m_seenShots.contains(projectile.id)) {
                    m_seenShots.insert(projectile.id);
                    return projectile;
                }
            }
        }
        return std::nullopt;
    }

    // Ticks until `witch` drinks (at most `patience` ticks), calling `each` before every tick;
    // returns the potion it holds, or qcNoItem.
    QcItemId awaitDrink(quint32 witch, const std::function<void()> &each = {})
    {
        for (int tick = 0; tick < patience && !mob(witch)->archer->drinking(); ++tick) {
            if (each) {
                each();
            }
            ticks(1);
        }
        return mob(witch)->archer->drinking() ? mob(witch)->archer->drinkItem : qcNoItem;
    }

    // Drops a potion of `kind` from just above `where` straight down, thrown by `shooter`, and ticks
    // until it burst.
    void burst(const char *kind, const QcVec3 &where, quint32 shooter)
    {
        QcProjectileManager::Launch launch;
        launch.shooter = shooter;
        launch.type = u"qtcraft:"_s + QString::fromLatin1(kind);
        launch.position = where + QcVec3{0.0, 0.6, 0.0};
        launch.velocity = {0.0, -8.0, 0.0};
        QVERIFY(m_server->entities().projectiles().launch(launch, m_server->entities().newId()));
        for (int tick = 0; tick < 20 && !m_server->entities().projectiles().all().isEmpty(); ++tick) {
            ticks(1);
        }
        QVERIFY(m_server->entities().projectiles().all().isEmpty());
    }

    // The seven kinds of the witch's loot table.
    QSet<QcItemId> lootKinds() const
    {
        return {item("qtcraft:glowstone_dust"), item("qtcraft:sugar"),     item("qtcraft:signal_dust"), item("qtcraft:spider_eye"),
                item("qtcraft:glass_bottle"),   item("qtcraft:gunpowder"), item("qtcraft:stick")};
    }

    int poisonTicksOf(quint32 creature)
    {
        const QcCreature *found = mob(creature);
        const QcStatusEffect *poison = found && found->effects.has() ? found->effects->find(u"qtcraft:poison"_s) : nullptr;
        return poison ? poison->ticksLeft : 0;
    }

    static bool parses(const char *json)
    {
        const QcResult<QcMobDefinition> mob = QcContentParser::parseMob(QJsonDocument::fromJson(json).object(), u"qtcraft"_s);
        if (!mob) {
            qInfo().noquote() << mob.error();
        }
        return mob.has_value();
    }

private slots:
    void cleanup()
    {
        m_seenShots.clear();
        m_client.reset();
        m_server.reset();
    }

    void witchesComeFromData()
    {
        const QcMobDefinition &witch = witchKind();
        QCOMPARE(witch.ai, QcMobDefinition::Ai::Archer);
        QCOMPARE(witch.archer.projectile, u"qtcraft:harming_splash_potion"_s);
        QCOMPARE(witch.archer.choices.size(), 3);
        const QcArcherDefinition::Choice &slowness = witch.archer.choices[0];
        QCOMPARE(slowness.projectile, u"qtcraft:slowness_splash_potion"_s);
        QCOMPARE(slowness.minDistance, 8.0);
        QCOMPARE(witch.archer.choices[1].minTargetHealth, 8.0);
        QCOMPARE(witch.archer.choices[2].maxDistance, 3.0);
        QCOMPARE(witch.archer.choices[2].chance, 0.25);
        QVERIFY(witch.drinks);
        QCOMPARE(witch.drinks->potions.size(), 4);
        QCOMPARE(witch.drinks->potions[0].itemId, item("qtcraft:water_breathing_potion"));
        QCOMPARE(witch.drinks->potions[0].need, QcMobDrinkDefinition::Need::Underwater);
        QCOMPARE(witch.drinks->potions[2].itemId, item("qtcraft:healing_potion"));
        QCOMPARE(witch.drinks->potions[3].distance, 11.0);
        QVERIFY(witch.immuneTo.contains(u"qtcraft:poison"_s));
        QCOMPARE(witch.attackDamage, 0.0);
        // Natural spawning like any monster again, with the reference's weight 5.
        QCOMPARE(witch.spawnWeight, 5.0);
        QCOMPARE(witch.drops.size(), 1);
        QCOMPARE(witch.drops.front().table, u"qtcraft:witch"_s);
        const QcProjectileDefinition *poison = m_content->projectile(u"qtcraft:poison_splash_potion"_s);
        QVERIFY(poison && poison->breaks());
        QCOMPARE(poison->effect(QcHitEffect::splash)->number, 4.0);

        // A splash breaks the projectile even when the file names no "break".
        const QcResult<QcProjectileDefinition> bare = QcProjectileParser::parseProjectile(
            QJsonDocument::fromJson(R"({"name":"p","item":"p","onHit":[{"type":"splash","radius":2}]})").object(), u"test"_s);
        QVERIFY(bare && bare->breaks());
    }

    void badDrinksAndChoicesFailToLoad()
    {
        // Drinking goes with the archer AI, a potion needs an item and a known need, a choice
        // both of its names; a drop is an item or a table.
        QVERIFY(parses(
            R"({"name":"a","skeleton":"humanoid","outfit":"zombie","hostile":true,"ai":"archer","archer":{"projectile":"arrow","choices":[{"projectile":"snowball","unlessTargetHas":"poison","minDistance":2}]},"drinks":{"potions":[{"item":"milk_bucket","need":"hurt"}]}})"));
        QVERIFY(!parses(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","hostile":true,"drinks":{"potions":[{"item":"milk_bucket","need":"hurt"}]}})"));
        QVERIFY(!parses(
            R"({"name":"a","skeleton":"humanoid","outfit":"zombie","hostile":true,"ai":"archer","archer":{"projectile":"arrow","choices":[{"projectile":"snowball"}]}})"));
        QVERIFY(!parses(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","hostile":true,"ai":"archer","archer":{"projectile":"arrow"},"drinks":{}})"));
        QVERIFY(!parses(
            R"({"name":"a","skeleton":"humanoid","outfit":"zombie","hostile":true,"ai":"archer","archer":{"projectile":"arrow"},"drinks":{"potions":[{"item":"x","need":"thirsty"}]}})"));
        QVERIFY(!parses(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","drops":[{"item":"x","table":"y"}]})"));
        QVERIFY(parses(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","drops":[{"table":"y"}]})"));
    }

    void choiceFollowsTheTarget()
    {
        // Like the reference: slowness from 8 blocks while the target is not slowed, poison while it
        // has 8 health and no poison, weakness within 3 blocks by a quarter's chance, else harming.
        const QcArcherDefinition &archer = witchKind().archer;
        QcMobManager::PlayerView target;
        QcStatusEffects effects;
        target.effects = &effects;
        const auto choose = [&](double distance, double draw) { return QcArcherBrain::projectileFor(archer, target, distance, draw); };
        QCOMPARE(choose(9.0, 0.9), u"qtcraft:slowness_splash_potion"_s);
        QCOMPARE(choose(7.9, 0.9), u"qtcraft:poison_splash_potion"_s);
        QVERIFY(effects.give({u"qtcraft:slowness"_s, 1, 100}));
        QCOMPARE(choose(9.0, 0.9), u"qtcraft:poison_splash_potion"_s);
        QVERIFY(effects.give({u"qtcraft:poison"_s, 1, 100}));
        QCOMPARE(choose(5.0, 0.1), u"qtcraft:harming_splash_potion"_s);
        QCOMPARE(choose(2.5, 0.1), u"qtcraft:weakness_splash_potion"_s);
        QCOMPARE(choose(2.5, 0.3), u"qtcraft:harming_splash_potion"_s);
        // A weak target is not poisoned again but harmed.
        QVERIFY(effects.clear());
        target.health = 7.0;
        QCOMPARE(choose(5.0, 0.9), u"qtcraft:harming_splash_potion"_s);
    }

    void aWitchPoisonsFirstThenHarms()
    {
        joinArena();
        chat(u"/gamemode survival"_s);
        // Slowed and weakened already, so neither distance nor chance changes her choice.
        QVERIFY(QcServerEffects::givePlayer(*m_content, player(), {u"qtcraft:slowness"_s, 1, 6000}));
        QVERIFY(QcServerEffects::givePlayer(*m_content, player(), {u"qtcraft:weakness"_s, 1, 6000}));
        const quint32 witch = spawn("witch", 0.0, -7.0);
        const std::optional<QcProjectile> first = nextShot(witch, 200);
        QVERIFY(first);
        QCOMPARE(first->type, u"qtcraft:poison_splash_potion"_s);
        // It bursts at the player, who is poisoned now; the next potion harms.
        for (int tick = 0; tick < 40 && !player().effects.find(u"qtcraft:poison"_s); ++tick) {
            ticks(1);
        }
        QVERIFY(player().effects.find(u"qtcraft:poison"_s));
        const std::optional<QcProjectile> second = nextShot(witch, 200);
        QVERIFY(second);
        QCOMPARE(second->type, u"qtcraft:harming_splash_potion"_s);
    }

    void aSplashReachesAroundButSparesTheThrower()
    {
        joinArena();
        chat(u"/gamemode survival"_s);
        const QcVec3 centre = player().motion.position;
        const quint32 thrower = spawn("pig", 1.5, 0.0);
        const quint32 near = spawn("pig", -2.0, 0.0);
        const quint32 far = spawn("pig", 0.0, 6.0);
        burst("poison_splash_potion", centre + QcVec3{0.0, 1.0, 0.0}, thrower);

        // The player, hit in the middle, gets nearly the whole 45 s, the pig 2 m away less, the
        // thrower and a pig beyond the radius nothing.
        const QcStatusEffect *onPlayer = player().effects.find(u"qtcraft:poison"_s);
        QVERIFY(onPlayer);
        QVERIFY2(onPlayer->ticksLeft > 855 && onPlayer->ticksLeft <= 900, qPrintable(QString::number(onPlayer->ticksLeft)));
        const int nearTicks = poisonTicksOf(near);
        QVERIFY2(nearTicks > 100 && nearTicks < onPlayer->ticksLeft, qPrintable(QString::number(nearTicks)));
        QCOMPARE(poisonTicksOf(thrower), 0);
        QCOMPARE(poisonTicksOf(far), 0);
    }

    void harmingGoesThroughArmour()
    {
        joinArena();
        chat(u"/gamemode survival"_s);
        QcEquipment equipment;
        equipment.at(QcEquipmentSlot::Chest) = stack("qtcraft:diamond_chestplate", 1);
        player().equipment = equipment;
        const double before = player().health;
        const quint32 pig = spawn("pig", 0.0, 2.5);
        const double pigBefore = mob(pig)->health;
        burst("harming_splash_potion", player().motion.position + QcVec3{0.0, 1.0, 0.0}, 0);
        // Inside the player's body: the whole 6, armour or not; the pig 2.2 m from its box less. The
        // effect acts in the tick after the burst.
        ticks(1);
        QCOMPARE(player().health, before - 6.0);
        QVERIFY(mob(pig)->health < pigBefore && mob(pig)->health > pigBefore - 4.0);
    }

    void aHurtWitchDrinksHealingAndStands()
    {
        joinArena();
        chat(u"/gamemode creative"_s);
        const quint32 witch = spawn("witch", 0.0, -5.0);
        ticks(patience / 4);
        QVERIFY(!mob(witch)->archer->drinking());

        // Hurt, she drinks healing by chance, holds it where the client sees it and stands.
        mob(witch)->health = 18.0;
        QCOMPARE(awaitDrink(witch), item("qtcraft:healing_potion"));
        QCOMPARE(m_client->held.value(witch), item("qtcraft:healing_potion"));
        const QcVec3 at = mob(witch)->motion.position;
        ticks(drinkTicks() - 2);
        QVERIFY((mob(witch)->motion.position - at).length() < 0.05);

        // The potion's instant health heals her by 4 in the tick the drink ends, and the bottle
        // goes away; at full health she drinks nothing more, and the client sees an empty hand.
        ticks(1);
        QVERIFY(mob(witch)->archer->drinking());
        ticks(1);
        QCOMPARE(mob(witch)->health, 22.0);
        QCOMPARE(m_server->creatures().drinking().drinks(), 1);
        QVERIFY(!mob(witch)->archer->drinking());
        QCOMPARE(mob(witch)->archer->drinkItem, qcNoItem);
        mob(witch)->health = witchKind().maxHealth;
        ticks(3);
        QVERIFY(!mob(witch)->archer->drinking());
        QVERIFY(m_client->held.value(witch, qcNoItem) == qcNoItem);
    }

    void underWaterSheDrinksWaterBreathing()
    {
        joinArena();
        chat(u"/gamemode creative"_s);
        const quint32 witch = spawn("witch", 0.0, -5.0);
        // Two blocks of water in a stone well with a lid, so her eyes stay under water.
        const QcBlockPos base = m_feet.offset(0, 0, -5);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dy = 0; dy <= 2; ++dy) {
                    const bool inside = dx == 0 && dz == 0 && dy < 2;
                    m_server->world().setBlock(base.offset(dx, dy, dz), inside ? id("qtcraft:water") : id("qtcraft:stone"));
                }
            }
        }
        QCOMPARE(awaitDrink(witch), item("qtcraft:water_breathing_potion"));
        ticks(drinkTicks() + 1);
        const QcStatusEffect *breathing = mob(witch)->effects.has() ? mob(witch)->effects->find(u"qtcraft:water_breathing"_s) : nullptr;
        QVERIFY(breathing);
        QVERIFY(breathing->ticksLeft > 3500);
        // With it she loses no air, and she does not drink it again while it lasts.
        const int air = mob(witch)->air.air();
        ticks(100);
        QVERIFY(mob(witch)->air.air() >= air);
        QVERIFY(!mob(witch)->archer->drinking());
    }

    void burningSheDrinksFireResistance()
    {
        joinArena();
        chat(u"/gamemode creative"_s);
        const quint32 witch = spawn("witch", 0.0, -5.0);
        // Kept at full health, so only the fire is a need.
        const QcItemId drunk = awaitDrink(witch, [&] {
            mob(witch)->burnTicks = 200;
            mob(witch)->health = witchKind().maxHealth;
        });
        QCOMPARE(drunk, item("qtcraft:fire_resistance_potion"));
        ticks(drinkTicks() + 1);
        QVERIFY(mob(witch)->effects.has() && mob(witch)->effects->find(u"qtcraft:fire_resistance"_s));
    }

    void farFromHerTargetSheDrinksSwiftness()
    {
        joinArena();
        chat(u"/gamemode survival"_s);
        const quint32 witch = spawn("witch", 0.0, -15.5);
        QCOMPARE(awaitDrink(witch, [&] { mob(witch)->health = witchKind().maxHealth; }), item("qtcraft:speed_potion"));
        ticks(drinkTicks() + 1);
        QVERIFY(mob(witch)->effects.has() && mob(witch)->effects->find(u"qtcraft:speed"_s));
    }

    void aDrinkingWitchThrowsNothing()
    {
        // Within her range of a survival player she draws; a drink begun mid-draw ends the draw,
        // and she neither draws nor throws until the drink is over. Then she throws again.
        joinArena();
        chat(u"/gamemode survival"_s);
        const quint32 witch = spawn("witch", 0.0, -6.0);
        for (int tick = 0; tick < 100 && !mob(witch)->archer->drawing(); ++tick) {
            ticks(1);
        }
        QVERIFY(mob(witch)->archer->drawing());
        QVERIFY(m_server->entities().projectiles().all().isEmpty());
        mob(witch)->archer->drinkStart = static_cast<qint64>(m_server->tickCount());
        mob(witch)->archer->drinkItem = item("qtcraft:healing_potion");
        for (int tick = 0; tick < drinkTicks() - 1; ++tick) {
            ticks(1);
            QVERIFY(mob(witch)->archer->drinking());
            QVERIFY(m_server->entities().projectiles().all().isEmpty());
            QVERIFY(!mob(witch)->archer->drawing());
        }
        QVERIFY(nextShot(witch, 100).has_value());
    }

    void aWitchHasNoBlows()
    {
        // Next to a survival player she is no threat in melee: she never strikes, and kept
        // drinking she throws nothing either.
        joinArena();
        chat(u"/gamemode survival"_s);
        const quint32 witch = spawn("witch", 0.0, -1.0);
        const double before = player().health;
        for (int tick = 0; tick < 60; ++tick) {
            mob(witch)->archer->drinkStart = static_cast<qint64>(m_server->tickCount());
            mob(witch)->archer->drinkItem = item("qtcraft:healing_potion");
            ticks(1);
            QVERIFY(m_server->entities().projectiles().all().isEmpty());
        }
        QCOMPARE(player().health, before);
    }

    void theLootTableIsTheReferences()
    {
        // One to three draws of seven items with 0–2 each, sticks twice as likely.
        const QSet<QcItemId> kinds = lootKinds();
        QHash<QcItemId, int> counts;
        const int draws = 4000;
        for (int seed = 0; seed < draws; ++seed) {
            int total = 0;
            const QList<QcItemStack> loot = QcContentLoot::draw(*m_content, u"qtcraft:witch"_s, static_cast<quint64>(seed) * 7919u);
            for (const QcItemStack &stack : loot) {
                QVERIFY(kinds.contains(stack.item));
                total += stack.count;
                counts[stack.item] += stack.count;
            }
            QVERIFY(total <= 6);
        }
        // Expected per witch: 2 draws × 1/8 × 1 = 0.25 of each item, 0.5 sticks.
        for (const QcItemId kind : kinds) {
            const double each = static_cast<double>(counts.value(kind)) / draws;
            const double expected = kind == item("qtcraft:stick") ? 0.5 : 0.25;
            QVERIFY2(std::abs(each - expected) < 0.06, qPrintable(u"%1: %2"_s.arg(kind).arg(each)));
        }
    }

    void deadWitchesLeaveTheirLoot()
    {
        joinArena();
        chat(u"/gamemode creative"_s);
        QList<quint32> witches;
        for (int i = 0; i < 10; ++i) {
            const quint32 witch = spawn("witch", static_cast<double>(i - 5), -6.0);
            QVERIFY(QcServerEffects::giveCreature(*m_content, m_server->entities().creatures(), witch, {u"qtcraft:instant_damage"_s, 4, 1}));
            witches.append(witch);
        }
        ticks(3);
        for (const quint32 witch : witches) {
            QVERIFY(!m_server->mobs().find(witch));
        }
        // One to three draws of 0–2 each per witch, all of the seven kinds of her table.
        int items = 0;
        for (const QcItemEntity &entity : m_server->items().items()) {
            QVERIFY(lootKinds().contains(entity.stack.item));
            items += entity.stack.count;
        }
        QVERIFY(items > 0);
        QVERIFY(items <= 10 * 6);
    }

    void aWitchDyingWhileSheDrinksDropsHerPotion_data()
    {
        QTest::addColumn<double>("chance");
        QTest::addColumn<int>("potions");
        QTest::newRow("always") << 1.0 << 1;
        QTest::newRow("never") << 0.0 << 0;
    }

    void aWitchDyingWhileSheDrinksDropsHerPotion()
    {
        // Like the reference's held items, the potion she holds falls by the chance of her data
        // (0.085 in the base game, decision E-1105); here forced to always or never.
        QFETCH(double, chance);
        QFETCH(int, potions);
        QCOMPARE(witchKind().drinks->dropChance, 0.085);
        const std::shared_ptr<const QcContentRegistry> base = m_content;
        const auto restore = qScopeGuard([this, base] { m_content = base; });
        auto content = std::make_shared<QcContentRegistry>(*base);
        QcMobDefinition witchWithChance = base->mobs()[type("witch")];
        witchWithChance.drinks->dropChance = chance;
        content->registerMob(witchWithChance);
        m_content = content;

        joinArena();
        chat(u"/gamemode creative"_s);
        const quint32 witch = spawn("witch", 0.0, -6.0);
        mob(witch)->archer->drinkStart = static_cast<qint64>(m_server->tickCount());
        mob(witch)->archer->drinkItem = item("qtcraft:healing_potion");
        QVERIFY(QcServerEffects::giveCreature(*m_content, m_server->entities().creatures(), witch, {u"qtcraft:instant_damage"_s, 4, 1}));
        ticks(3);
        QVERIFY(!mob(witch));
        int dropped = 0;
        for (const QcItemEntity &entity : m_server->items().items()) {
            dropped += entity.stack.item == item("qtcraft:healing_potion") ? entity.stack.count : 0;
        }
        QCOMPARE(dropped, potions);
    }

    void measureTickCost()
    {
        // Thread time per tick of an arena with a player and nothing, ten skeletons or ten witches
        // (throwing, splashing, drinking): the witch costs about what an archer costs.
        const auto threadSeconds = [] {
            return static_cast<double>(QcThreadCpuClock::nowNs()) / 1e9;
        };
        const auto perTick = [&](const char *kind) {
            joinArena();
            chat(u"/gamemode survival"_s);
            for (int i = 0; kind && i < 10; ++i) {
                spawn(kind, static_cast<double>(i - 5) * 1.5, -9.0);
            }
            ticks(20);
            const double start = threadSeconds();
            const int count = 400;
            for (int i = 0; i < count; ++i) {
                player().health = 20.0;
                ticks(1);
            }
            return (threadSeconds() - start) / count * 1000.0;
        };
        const double empty = perTick(nullptr);
        const double skeletons = perTick("skeleton");
        const double witches = perTick("witch");
        qInfo().noquote() << u"ms per tick (thread time, with the test's message pump): no creature %1, 10 skeletons %2, 10 witches %3"_s
                                 .arg(empty, 0, 'f', 3)
                                 .arg(skeletons, 0, 'f', 3)
                                 .arg(witches, 0, 'f', 3);
    }
};

QTEST_GUILESS_MAIN(TestWitches)
#include "test_witches.moc"
