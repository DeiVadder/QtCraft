// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/items/QcPotionParser.h"
#include "common/physics/QcPhysics.h"
#include "fixtures/QcMachineFixture.h"
#include "server/machines/QcBrewingRules.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::StringLiterals;

// The brewing stand (decisions E-1061 to E-1063): potions and recipes come from data, a brew takes
// 20 s and one of the fuel's 20 uses, a change of the ingredient stops it, and hoppers and
// comparators reach the stand as they reach a furnace.
class TestBrewing : public QcMachineFixture
{
    Q_OBJECT

private:
    static constexpr int brewTicks = static_cast<int>(20.0 * QcPhysics::tickRate);

    QList<QcItemStack> bottles(const char *name) const { return {stack(name, 1), stack(name, 1), stack(name, 1)}; }

    // A stand at local (0, y, 0) with `ingredient`, `fuel` and three bottles of `bottle`.
    void stand(int y, const QcItemStack &ingredient, const QcItemStack &fuel, const char *bottle)
    {
        put(0, y, 0, u"brewing_stand"_s);
        fill(0, y, 0, QList<QcItemStack>{ingredient, fuel} + bottles(bottle));
    }

    const QcBlockEntities::Machine *machineAt(int x, int y, int z) const { return m_server->blockEntities().machine(at(x, y, z)); }

    const QcBrewerDefinition &brewer() const { return *m_content->block(*m_content->blockId(u"qtcraft:brewing_stand"_s)).machine.brewing; }

    // What `ingredient` makes of `bottle`, as the item's name; empty when it makes nothing.
    QString mixed(const QcBrewingRules &rules, const QString &bottle, const QString &ingredient) const
    {
        const std::optional<QcItemId> result = rules.mix(*m_content->itemId(u"qtcraft:"_s + bottle), *m_content->itemId(u"qtcraft:"_s + ingredient));
        return result ? m_content->item(*result).name.section(u':', 1) : QString();
    }

private slots:
    void init() { buildArea(); }

    void potionsBecomeItemsWithTheIdsOfTheEarlierReleases()
    {
        for (const char *name : {"qtcraft:healing_potion", "qtcraft:harming_splash_potion", "qtcraft:poison_splash_potion", "qtcraft:water_bottle", "qtcraft:speed_potion", "qtcraft:speed_splash_potion", "qtcraft:speed_lingering_potion", "qtcraft:long_speed_potion", "qtcraft:strong_speed_splash_potion"}) {
            QVERIFY2(m_content->itemId(QString::fromLatin1(name)), name);
        }
        QVERIFY(!m_content->itemId(u"qtcraft:water_splash_potion"_s));

        const QcPotionCatalog &catalog = m_content->potionCatalog();
        const std::optional<QcPotionCatalog::ItemPotion> poison = catalog.potionOf(item("qtcraft:poison_splash_potion"));
        QVERIFY(poison);
        QCOMPARE(poison->potion, u"qtcraft:poison"_s);
        QCOMPARE(poison->form, QcPotionForm::Splash);
        QCOMPARE(catalog.potionOf(item("qtcraft:water_bottle"))->potion, u"qtcraft:water"_s);

        const QcItemDefinition &speed = m_content->item(*m_content->itemId(u"qtcraft:speed_potion"_s));
        QCOMPARE(speed.maxStack, 1);
        QCOMPARE(speed.drink->effects.size(), 1);
        QCOMPARE(speed.drink->effects[0].seconds, 180.0);
        QVERIFY(m_content->projectile(u"qtcraft:speed_lingering_potion"_s));
    }

    void aLingeringPotionLastsAQuarterOfTheTime()
    {
        const QcProjectileDefinition *lingering = m_content->projectile(u"qtcraft:speed_lingering_potion"_s);
        const QcProjectileDefinition *splash = m_content->projectile(u"qtcraft:speed_splash_potion"_s);
        QVERIFY(lingering && splash);
        QCOMPARE(splash->effect(QcHitEffect::effect)->number, 180.0);
        QCOMPARE(lingering->effect(QcHitEffect::effect)->number, 45.0);
    }

    void theRecipesOfTheRoleModel_data()
    {
        QTest::addColumn<QString>("bottle");
        QTest::addColumn<QString>("ingredient");
        QTest::addColumn<QString>("result");
        const QList<std::array<const char *, 3>> rows = {
            {"water_bottle", "nether_wart", "awkward_potion"},
            {"water_bottle", "glowstone_dust", "thick_potion"},
            {"water_bottle", "sugar", "mundane_potion"},
            {"water_bottle", "fermented_spider_eye", "weakness_potion"},
            {"awkward_potion", "sugar", "speed_potion"},
            {"awkward_potion", "gilded_melon_slice", "healing_potion"},
            {"awkward_potion", "spider_eye", "poison_potion"},
            {"awkward_potion", "magma_cream", "fire_resistance_potion"},
            {"awkward_potion", "golden_carrot", "night_vision_potion"},
            {"awkward_potion", "ember_dust", "strength_potion"},
            {"awkward_potion", "wailer_tear", "regeneration_potion"},
            {"speed_potion", "signal_dust", "long_speed_potion"},
            {"speed_potion", "glowstone_dust", "strong_speed_potion"},
            {"speed_potion", "gunpowder", "speed_splash_potion"},
            {"long_speed_potion", "gunpowder", "long_speed_splash_potion"},
            {"speed_potion", "fermented_spider_eye", "slowness_potion"},
            {"speed_splash_potion", "fermented_spider_eye", "slowness_splash_potion"},
            {"healing_potion", "fermented_spider_eye", "harming_potion"},
            {"poison_potion", "fermented_spider_eye", "harming_potion"},
            {"night_vision_potion", "fermented_spider_eye", "invisibility_potion"},
            {"strong_healing_potion", "fermented_spider_eye", "strong_harming_potion"},
            {"long_speed_potion", "glowstone_dust", ""},
            {"strong_speed_potion", "signal_dust", ""},
            {"speed_splash_potion", "gunpowder", ""},
            {"awkward_potion", "nether_wart", ""},
            {"healing_potion", "signal_dust", ""},
        };
        for (const auto &[bottle, ingredient, result] : rows) {
            QTest::addRow("%s %s", bottle, ingredient) << QString::fromLatin1(bottle) << QString::fromLatin1(ingredient) << QString::fromLatin1(result);
        }
    }

    void theRecipesOfTheRoleModel()
    {
        QFETCH(QString, bottle);
        QFETCH(QString, ingredient);
        QFETCH(QString, result);
        const QcBrewingRules rules(m_content);
        QCOMPARE(mixed(rules, bottle, ingredient), result);
    }

    void aModPotionGetsTheCommonStepsFromItsVariant()
    {
        const QJsonObject json = QJsonDocument::fromJson(R"({"name":"glow","variantOf":"speed","variant":"strong"})").object();
        const QcResult<QcPotionDefinition> potion = QcPotionParser::parsePotion(json, u"mod"_s);
        QVERIFY(potion.has_value());
        QCOMPARE(potion->variantOf, u"mod:speed"_s);
        QVERIFY(potion->hasForm(QcPotionForm::Lingering));

        const auto refused = [](const char *text) {
            return !QcPotionParser::parseBrewingRecipe(QJsonDocument::fromJson(text).object(), u"mod"_s).has_value();
        };
        QVERIFY(refused(R"({"name":"a","ingredient":"sugar"})"));
        QVERIFY(refused(R"({"name":"a","ingredient":"sugar","variant":"long","form":"splash"})"));
        QVERIFY(refused(R"({"name":"a","ingredient":"sugar","form":"drink"})"));
        QVERIFY(refused(R"({"name":"a","ingredient":"sugar","base":"water"})"));
        QVERIFY(!refused(R"({"name":"a","ingredient":"sugar","base":"water","result":"thick"})"));
    }

    void aBrewTakesTwentySecondsAndUsesOneIngredient()
    {
        stand(0, stack("qtcraft:nether_wart", 2), stack("qtcraft:ember_dust", 2), "qtcraft:water_bottle");
        ticks(brewTicks - 2);
        QCOMPARE(countIn(0, 0, 0, "qtcraft:water_bottle"), 3);
        QCOMPARE(countIn(0, 0, 0, "qtcraft:nether_wart"), 2);
        QCOMPARE(countIn(0, 0, 0, "qtcraft:ember_dust"), 1);
        QCOMPARE(machineAt(0, 0, 0)->burnRemaining, 19.0);

        ticks(4);
        QCOMPARE(countIn(0, 0, 0, "qtcraft:awkward_potion"), 3);
        QCOMPARE(countIn(0, 0, 0, "qtcraft:nether_wart"), 1);

        // The wart does nothing to odd potions: nothing starts, no use is spent.
        ticks(5);
        QCOMPARE(machineAt(0, 0, 0)->progress, 0.0);
        QCOMPARE(machineAt(0, 0, 0)->burnRemaining, 19.0);
    }

    void oneDustLastsTwentyBrews()
    {
        const QcBrewingRules rules(m_content);
        QcBlockEntities::Machine machine;
        machine.stacks.resize(QcBrewerDefinition::slotCount);
        machine.stacks[QcBrewingRules::ingredientSlot] = stack("qtcraft:sugar", 64);
        machine.stacks[QcBrewingRules::fuelSlot] = stack("qtcraft:ember_dust", 1);

        int brews = 0;
        for (int step = 0; step < 200; ++step) {
            if (machine.stacks[QcBrewingRules::firstBottleSlot].item != item("qtcraft:awkward_potion")) {
                machine.stacks[QcBrewingRules::firstBottleSlot] = stack("qtcraft:awkward_potion", 1);
            }
            brews += rules.step(machine, brewer(), 10.0) == QcBrewingRules::Step::Brewed ? 1 : 0;
        }
        QCOMPARE(brews, 20);
        QVERIFY(machine.stacks[QcBrewingRules::fuelSlot].isEmpty());
        QCOMPARE(machine.burnRemaining, 0.0);
    }

    void fuelGoesInWithoutWork()
    {
        stand(0, {}, stack("qtcraft:ember_dust", 3), "qtcraft:water_bottle");
        ticks(3);
        QCOMPARE(countIn(0, 0, 0, "qtcraft:ember_dust"), 2);
        QCOMPARE(machineAt(0, 0, 0)->burnRemaining, 20.0);
    }

    void withoutFuelNothingBrews()
    {
        stand(0, stack("qtcraft:nether_wart", 1), {}, "qtcraft:water_bottle");
        ticks(brewTicks + 5);
        QCOMPARE(countIn(0, 0, 0, "qtcraft:water_bottle"), 3);
        QCOMPARE(machineAt(0, 0, 0)->progress, 0.0);
    }

    void changingTheIngredientStopsTheBrew()
    {
        stand(0, stack("qtcraft:sugar", 1), stack("qtcraft:ember_dust", 1), "qtcraft:awkward_potion");
        ticks(brewTicks / 2);
        QVERIFY(machineAt(0, 0, 0)->progress > 0.0);

        m_server->blockEntities().machine(at(0, 0, 0))->stacks[0] = stack("qtcraft:spider_eye", 1);
        ticks(1);
        QVERIFY(machineAt(0, 0, 0)->progress < 1.0);
        // The stopped brew stays paid: the new one is the second use.
        ticks(1);
        QCOMPARE(machineAt(0, 0, 0)->burnRemaining, 18.0);
        ticks(brewTicks - 3);
        QCOMPARE(countIn(0, 0, 0, "qtcraft:awkward_potion"), 3);
        ticks(4);
        QCOMPARE(countIn(0, 0, 0, "qtcraft:poison_potion"), 3);
    }

    void onlyTheBottlesItFitsChange()
    {
        put(0, 0, 0, u"brewing_stand"_s);
        fill(0, 0, 0, {stack("qtcraft:sugar", 1), stack("qtcraft:ember_dust", 1), stack("qtcraft:awkward_potion", 1), stack("qtcraft:speed_splash_potion", 1), {}});
        ticks(brewTicks + 3);
        QCOMPARE(countIn(0, 0, 0, "qtcraft:speed_potion"), 1);
        QCOMPARE(countIn(0, 0, 0, "qtcraft:speed_splash_potion"), 1);
    }

    void theStandShowsItsBottles()
    {
        put(0, 0, 0, u"brewing_stand"_s);
        fill(0, 0, 0, {{}, {}, stack("qtcraft:water_bottle", 1), {}, stack("qtcraft:water_bottle", 1)});
        ticks(2);
        QVERIFY(flag(0, 0, 0, u"bottle_0"_s));
        QVERIFY(!flag(0, 0, 0, u"bottle_1"_s));
        QVERIFY(flag(0, 0, 0, u"bottle_2"_s));

        // A stand set onto the stand keeps the slots, and shows them again.
        const QcBlockPos pos = at(0, 0, 0);
        chat(u"/setblock %1 %2 %3 brewing_stand"_s.arg(pos.x).arg(pos.y).arg(pos.z));
        ticks(2);
        QVERIFY(machineAt(0, 0, 0));
        QVERIFY(flag(0, 0, 0, u"bottle_0"_s));
        QVERIFY(flag(0, 0, 0, u"bottle_2"_s));
    }

    void hoppersFillFromAboveAndTheSides()
    {
        put(0, 0, 0, u"brewing_stand"_s);
        QVERIFY(ensureEntity(at(0, 0, 0)));
        put(0, 1, 0, u"signal_hopper[facing=down]"_s);
        fill(0, 1, 0, {stack("qtcraft:dirt", 1), stack("qtcraft:water_bottle", 1), stack("qtcraft:sugar", 2)});
        put(-1, 0, 0, u"signal_hopper[facing=east]"_s);
        fill(-1, 0, 0, {stack("qtcraft:sugar", 1), stack("qtcraft:ember_dust", 2), stack("qtcraft:water_bottle", 1)});
        ticks(60);

        const QList<QcItemStack> held = slotsAt(0, 0, 0);
        QCOMPARE(held[QcBrewingRules::ingredientSlot].item, item("qtcraft:sugar"));
        QCOMPARE(held[QcBrewingRules::ingredientSlot].count, 2);
        // From above only the ingredient; from the side bottles and fuel, no ingredient.
        QCOMPARE(countIn(0, 1, 0, "qtcraft:water_bottle"), 1);
        QCOMPARE(countIn(0, 1, 0, "qtcraft:dirt"), 1);
        QCOMPARE(countIn(-1, 0, 0, "qtcraft:sugar"), 1);
        QCOMPARE(countIn(-1, 0, 0, "qtcraft:water_bottle"), 0);
        QCOMPARE(countIn(0, 0, 0, "qtcraft:water_bottle") + countIn(0, 0, 0, "qtcraft:mundane_potion"), 1);
        QCOMPARE(countIn(-1, 0, 0, "qtcraft:ember_dust"), 0);
    }

    void aHopperBelowTakesTheBottlesOnly()
    {
        stand(1, stack("qtcraft:nether_wart", 1), stack("qtcraft:ember_dust", 2), "qtcraft:awkward_potion");
        put(0, 0, 0, u"signal_hopper[facing=down]"_s);
        ticks(60);
        QCOMPARE(countIn(0, 0, 0, "qtcraft:awkward_potion"), 3);
        QCOMPARE(countIn(0, 0, 0, "qtcraft:nether_wart"), 0);
        QCOMPARE(countIn(0, 0, 0, "qtcraft:ember_dust"), 0);
        QCOMPARE(countIn(0, 1, 0, "qtcraft:nether_wart"), 1);
    }

    void aComparatorReadsTheStand()
    {
        put(0, 0, 0, u"brewing_stand"_s);
        QVERIFY(ensureEntity(at(0, 0, 0)));
        put(1, 0, 0, u"signal_comparator[facing=east]"_s);
        ticks(3);
        QCOMPARE(power(1, 0, 0), 0);

        fill(0, 0, 0, QList<QcItemStack>{{}, {}} + bottles("qtcraft:water_bottle"));
        ticks(4);
        // Three of five slots full: floor(1 + 14 * 3 / 5).
        QCOMPARE(power(1, 0, 0), 9);
    }

    void aComparatorReadsTheLevelOfACauldron()
    {
        put(1, 0, 0, u"signal_comparator[facing=east]"_s);
        for (int level = 0; level <= 3; ++level) {
            put(0, 0, 0, u"cauldron[level=%1]"_s.arg(level));
            ticks(4);
            QCOMPARE(power(1, 0, 0), level);
        }
    }

    void bottlesKeepTheirCountThroughABrew()
    {
        // A potion a mod lets stack: nothing of a bottle slot is lost.
        put(0, 0, 0, u"brewing_stand"_s);
        fill(0, 0, 0, QList<QcItemStack>{stack("qtcraft:nether_wart", 1), stack("qtcraft:ember_dust", 1), stack("qtcraft:water_bottle", 2), {}, {}});
        ticks(brewTicks + 2);
        QCOMPARE(countIn(0, 0, 0, "qtcraft:awkward_potion"), 2);
    }

    void breakingTheStandDropsAllFiveSlots()
    {
        stand(0, stack("qtcraft:sugar", 4), stack("qtcraft:ember_dust", 2), "qtcraft:awkward_potion");
        put(0, 0, 0, u"air"_s);
        ticks(2);
        QVERIFY(!machineAt(0, 0, 0));
        QCOMPARE(everywhere("qtcraft:sugar"), 4);
        QCOMPARE(everywhere("qtcraft:ember_dust"), 2);
        QCOMPARE(everywhere("qtcraft:awkward_potion"), 3);
    }
};

QTEST_GUILESS_MAIN(TestBrewing)
#include "test_brewing.moc"
