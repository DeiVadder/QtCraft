// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/items/QcPotionParser.h"
#include "common/gameplay/QcCrafting.h"
#include "fixtures/QcMachineFixture.h"
#include "server/machines/QcBrewingRules.h"
#include "server/projectiles/QcHitEffects.h"
#include "server/projectiles/QcPlayerLaunchers.h"
#include "server/storage/QcEntityCodec.h"
#include "server/storage/QcEntityRecords.h"

#include <QJsonArray>
#include <QJsonObject>

using namespace Qt::StringLiterals;

// Tipped arrows (decisions E-1100 to E-1102): every potion with an effect and a splash form has
// its arrow, made of eight arrows around the splash potion; a hit gives the eighth part of the
// potion's time, from a bow as from a dispenser, and a stuck one comes back as itself.
class TestTippedArrows : public QcMachineFixture
{
    Q_OBJECT

private:
    QcServerClient &player() { return *m_server->entities().clients().front(); }
    QcProjectileManager &projectiles() { return m_server->entities().projectiles(); }

    // A pig on the floor at local (x, 0, z), which stands still long enough for a shot.
    quint32 pigAt(int x, int z)
    {
        const QcBlockPos cell = at(x, 0, z);
        const QcVec3 place{cell.x + 0.5, static_cast<double>(cell.y), cell.z + 0.5};
        return m_server->entities().creatures().spawn(*m_content->mobKind(u"qtcraft:pig"_s), place, m_server->entities().newId());
    }

    // The ticks of the effect `name` the creature `mob` has; 0 without it.
    int ticksOn(quint32 mob, const QString &name) const
    {
        const QcCreature *creature = m_server->entities().creatures().find(mob);
        if (!creature || !creature->effects) {
            return 0;
        }
        const QList<QcStatusEffect> effects = creature->effects->all();
        const auto it = std::ranges::find_if(effects, [&name](const QcStatusEffect &effect) { return effect.name == name; });
        return it == effects.end() ? 0 : it->ticksLeft;
    }

    double healthOf(quint32 mob) const { return m_server->entities().creatures().find(mob)->health; }

    void setHealth(quint32 mob, double health) { m_server->entities().creatures().find(mob)->health = health; }

    // A shot of the kind `type` by nobody, flying east along the row z of the area at `speed`.
    bool shoot(const QString &type, int x, int z, double speed)
    {
        const QcBlockPos cell = at(x, 0, z);
        return m_server->systems().projectiles.launch({0, type, {cell.x + 0.5, cell.y + 0.5, cell.z + 0.5}, {speed, 0.0, 0.0}});
    }

    // The hit of the arrow `name` at full strength.
    QcHitEffects::Impact impactOf(const QString &name) const
    {
        const QcProjectileDefinition *projectile = m_content->projectile(name);
        return projectile ? QcHitEffects().impactOf(*projectile, 6.0) : QcHitEffects::Impact{};
    }

    // Eight arrows around `middle` in a grid of three by three.
    QList<QcItemStack> ring(const char *middle) const
    {
        QList<QcItemStack> grid(9, stack("qtcraft:arrow", 1));
        grid[4] = stack(middle, 1);
        return grid;
    }

    // The file of a potion `name` of `effect` for 16 s.
    static QJsonObject potionFile(const QString &name, const QString &effect)
    {
        const QJsonObject given{{u"effect"_s, effect}, {u"seconds"_s, 16}};
        return {{u"name"_s, name}, {u"effects"_s, QJsonArray{given}}};
    }

    // The base content with `potion` as the file of a mod's potion.
    static std::shared_ptr<QcContentRegistry> contentWith(const QJsonObject &potion)
    {
        auto content = std::make_shared<QcContentRegistry>();
        if (!QcBaseContent::loadInto(*content)) {
            QcTestUtil::failCase(QStringLiteral("cannot load the base content"));
        }
        content->potionCatalog().registerPotion(*QcPotionParser::parsePotion(potion, u"mod"_s));
        static_cast<void>(content->finish());
        return content;
    }

private slots:
    void init() { buildArea(); }

    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void everyPotionWithAnEffectAndASplashFormHasItsArrow()
    {
        int arrows = 0;
        const QList<QcPotionDefinition> potions = m_content->potionCatalog().potions();
        for (const QcPotionDefinition &potion : potions) {
            const QString name = potion.name + u"_tipped_arrow"_s;
            const std::optional<QcItemId> id = m_content->itemId(name);
            QCOMPARE(id.has_value(), !potion.effects.isEmpty() && potion.hasForm(QcPotionForm::Splash));
            if (!id) {
                continue;
            }

            ++arrows;
            const QcItemDefinition &definition = m_content->item(*id);
            QCOMPARE(definition.projectile, name);
            QCOMPARE(definition.maxStack, 64);
            QCOMPARE(definition.dispense, QcDispenseAction::Shoot);
            QVERIFY(definition.tags.contains(u"qtcraft:arrows"_s));
            QVERIFY(!definition.throwable);
            QCOMPARE(m_content->projectile(name)->item, name);
        }
        // All 31 kinds but water and the three bases without an effect.
        QCOMPARE(potions.size(), 31);
        QCOMPARE(arrows, 27);
    }

    void aPackThatChangesThePlainArrowChangesThemToo()
    {
        auto content = std::make_shared<QcContentRegistry>();
        QVERIFY(QcBaseContent::loadInto(*content));
        QcProjectileDefinition plain = *content->projectile(u"qtcraft:arrow"_s);
        plain.gravity = 7.0;
        plain.despawnSeconds = 11.0;
        content->registerProjectile(plain);
        static_cast<void>(content->finish());

        const QcProjectileDefinition &tipped = *content->projectile(u"qtcraft:poison_tipped_arrow"_s);
        QCOMPARE(tipped.gravity, 7.0);
        QCOMPARE(tipped.despawnSeconds, 11.0);
        QCOMPARE(tipped.onHit.size(), plain.onHit.size() + 1);
    }

    void aPackKeepsItsOwnProjectile()
    {
        auto content = std::make_shared<QcContentRegistry>();
        QVERIFY(QcBaseContent::loadInto(*content));
        QcProjectileDefinition own = *content->projectile(u"qtcraft:arrow"_s);
        own.name = u"qtcraft:poison_tipped_arrow"_s;
        own.gravity = 3.0;
        content->registerProjectile(own);
        static_cast<void>(content->finish());

        const QcProjectileDefinition &kept = *content->projectile(own.name);
        QCOMPARE(kept.gravity, 3.0);
        QCOMPARE(kept.onHit.size(), own.onHit.size());
        QVERIFY(content->itemId(own.name));
    }

    void aHitDealsWhatThePlainArrowDeals()
    {
        const QcHitEffects::Impact impact = impactOf(u"qtcraft:poison_tipped_arrow"_s);
        QCOMPARE(impact.damage, 6.0);
        QCOMPARE(impact.knockback, 1.0);
    }

    void aHitGivesTheEighthPartOfThePotionsTime_data()
    {
        QTest::addColumn<QString>("arrow");
        QTest::addColumn<QString>("effect");
        QTest::addColumn<int>("level");
        QTest::addColumn<int>("ticks");
        const auto row = [](const char *name, const char *effect, int level, int ticks) {
            QTest::newRow(name) << u"qtcraft:%1_tipped_arrow"_s.arg(QString::fromLatin1(name)) << u"qtcraft:%1"_s.arg(QString::fromLatin1(effect)) << level << ticks;
        };
        row("poison", "poison", 1, 112); // 45 s: 900 ticks / 8, rounded down
        row("long_poison", "poison", 1, 225);
        row("strong_poison", "poison", 2, 54); // 21.6 s
        row("speed", "speed", 1, 450);
        row("long_speed", "speed", 1, 1200);
        row("strong_speed", "speed", 2, 225);
        row("slowness", "slowness", 1, 225);
        row("strong_slowness", "slowness", 4, 50); // 20 s
        row("regeneration", "regeneration", 1, 112);
        row("strong_regeneration", "regeneration", 2, 56); // 22.5 s
        row("weakness", "weakness", 1, 225);
    }

    void aHitGivesTheEighthPartOfThePotionsTime()
    {
        QFETCH(QString, arrow);
        QFETCH(QString, effect);
        QFETCH(int, level);
        QFETCH(int, ticks);
        const QcHitEffects::Impact impact = impactOf(arrow);
        QCOMPARE(impact.effects.size(), 1);
        QCOMPARE(impact.effects.front().name, effect);
        QCOMPARE(impact.effects.front().level, level);
        QCOMPARE(impact.effects.front().ticksLeft, ticks);
    }

    void instantEffectsKeepTheirStrength()
    {
        const QcHitEffects::Impact harming = impactOf(u"qtcraft:strong_harming_tipped_arrow"_s);
        QCOMPARE(harming.effects.size(), 1);
        QCOMPARE(harming.effects.front().name, u"qtcraft:instant_damage"_s);
        QCOMPARE(harming.effects.front().level, 2);
        QCOMPARE(harming.effects.front().strength, 1.0);
    }

    void eightArrowsAroundASplashPotionMakeEight()
    {
        const std::optional<qsizetype> recipe = QcCrafting::match(*m_content, ring("qtcraft:poison_splash_potion"), 3);
        QVERIFY(recipe);
        const QcRecipeDefinition &definition = m_content->recipes()[*recipe];
        QCOMPARE(definition.resultItem, u"qtcraft:poison_tipped_arrow"_s);
        QCOMPARE(definition.resultCount, 8);

        // The old splash ids make the arrow of their kind, and the bottle is used up.
        const QcRecipeDefinition &harming = m_content->recipes()[*QcCrafting::match(*m_content, ring("qtcraft:harming_splash_potion"), 3)];
        QCOMPARE(harming.resultItem, u"qtcraft:harming_tipped_arrow"_s);
    }

    void theBottleIsUsedUp()
    {
        QcInventory inventory;
        QCOMPARE(inventory.add(item("qtcraft:arrow"), 8, *m_content), 0);
        QCOMPARE(inventory.add(item("qtcraft:poison_splash_potion"), 1, *m_content), 0);
        const qsizetype recipe = *QcCrafting::match(*m_content, ring("qtcraft:poison_splash_potion"), 3);
        QVERIFY(QcCrafting::consume(*m_content, inventory, m_content->recipes()[recipe], 1));
        QCOMPARE(inventory.countOf(item("qtcraft:arrow")), 0);
        QCOMPARE(inventory.countOf(item("qtcraft:poison_splash_potion")), 0);
        QCOMPARE(inventory.countOf(item("qtcraft:glass_bottle")), 0);
    }

    void nothingElseTipsArrows()
    {
        // The lingering potion waits for the End; a drink and a tipped arrow are no ingredient.
        for (const char *middle : {"qtcraft:poison_lingering_potion", "qtcraft:poison_potion", "qtcraft:water_bottle", "qtcraft:awkward_potion", "qtcraft:awkward_splash_potion", "qtcraft:poison_tipped_arrow"}) {
            QVERIFY2(!QcCrafting::match(*m_content, ring(middle), 3), middle);
        }

        // Tipped arrows do not take a second potion.
        QList<QcItemStack> tipped(9, stack("qtcraft:poison_tipped_arrow", 1));
        tipped[4] = stack("qtcraft:speed_splash_potion", 1);
        QVERIFY(!QcCrafting::match(*m_content, tipped, 3));

        QList<QcItemStack> seven = ring("qtcraft:poison_splash_potion");
        seven[0] = {};
        QVERIFY(!QcCrafting::match(*m_content, seven, 3));
    }

    void aPotionOfAModGetsItsArrowToo()
    {
        const auto content = contentWith(potionFile(u"venom"_s, u"qtcraft:poison"_s));
        const std::optional<QcItemId> arrow = content->itemId(u"mod:venom_tipped_arrow"_s);
        QVERIFY(arrow);
        const QcLauncherDefinition &bow = *content->item(*content->itemId(u"qtcraft:bow"_s)).launcher;
        QVERIFY(bow.takes(content->item(*arrow).name, content->item(*arrow).tags));
        QVERIFY(content->recipeIndex(u"mod:venom_tipped_arrow"_s));
        QCOMPARE(content->recipes()[*content->recipeIndex(u"mod:venom_tipped_arrow"_s)].itemAt(1, 1), u"mod:venom_splash_potion"_s);
        QVERIFY(content->assets().texture(u"mod:venom_tipped_arrow"_s));
    }

    void aPotionWithoutASplashFormHasNoArrow()
    {
        QJsonObject tonic = potionFile(u"tonic"_s, u"qtcraft:speed"_s);
        tonic.insert(u"forms"_s, QJsonArray{u"drink"_s});
        const auto content = contentWith(tonic);
        QVERIFY(content->itemId(u"mod:tonic_potion"_s));
        QVERIFY(!content->itemId(u"mod:tonic_tipped_arrow"_s));
    }

    void contentWithoutThePlainArrowHasNone()
    {
        auto content = std::make_shared<QcContentRegistry>();
        content->potionCatalog().registerPotion(*QcPotionParser::parsePotion(potionFile(u"venom"_s, u"qtcraft:poison"_s), u"qtcraft"_s));
        static_cast<void>(content->finish());
        QVERIFY(!content->itemId(u"qtcraft:venom_tipped_arrow"_s));
    }

    void theirNameSaysThePotion()
    {
        const QcTranslations &texts = m_content->translations();
        QCOMPARE(texts.translate(u"item.qtcraft.poison_tipped_arrow"_s, u"de"_s), u"Pfeil der Vergiftung"_s);
        QCOMPARE(texts.translate(u"item.qtcraft.long_poison_tipped_arrow"_s, u"de"_s), u"Pfeil der Vergiftung (lang)"_s);
        QVERIFY(texts.has(u"item.qtcraft.strong_speed_tipped_arrow"_s, u"en"_s));
    }

    void aBowShootsThemAndTheHitPoisons()
    {
        const quint32 pig = pigAt(6, 5);
        player().motion.position = {at(2, 0, 5).x + 0.5, static_cast<double>(at(2, 0, 5).y), at(2, 0, 5).z + 0.5};
        player().inventory.add(item("qtcraft:bow"), 1, *m_content);
        player().inventory.add(item("qtcraft:poison_tipped_arrow"), 2, *m_content);
        // Down from the eyes to the pig's body four blocks away.
        player().yaw = 90.0f;
        player().pitch = -12.0f;

        QVERIFY(m_server->systems().projectiles.launchers().shoot(player(), 1.0));
        QCOMPARE(projectiles().all().size(), 1);
        QCOMPARE(projectiles().all().front().type, u"qtcraft:poison_tipped_arrow"_s);
        QCOMPARE(player().inventory.countOf(item("qtcraft:poison_tipped_arrow")), 1);

        ticks(5);
        QVERIFY(projectiles().all().isEmpty());
        QVERIFY(ticksOn(pig, u"qtcraft:poison"_s) > 100);
    }

    void aDispenserShootsThemAndTheHitGivesTheEffect()
    {
        const quint32 pig = pigAt(8, 5);
        put(4, 0, 5, u"signal_dispenser[facing=east]"_s);
        put(4, 0, 6, u"lever[attach=floor]"_s);
        fill(4, 0, 5, {stack("qtcraft:slowness_tipped_arrow", 3)});
        ticks(2);

        flip(4, 0, 6);
        ticks(5);
        QCOMPARE(projectiles().all().size(), 1);
        QCOMPARE(projectiles().all().front().type, u"qtcraft:slowness_tipped_arrow"_s);
        QCOMPARE(countIn(4, 0, 5, "qtcraft:slowness_tipped_arrow"), 2);

        ticks(15);
        QVERIFY(projectiles().all().isEmpty());
        QVERIFY(ticksOn(pig, u"qtcraft:slowness"_s) > 200);
    }

    void aPlainArrowFromADispenserGivesNoEffect()
    {
        const quint32 pig = pigAt(8, 5);
        put(4, 0, 5, u"signal_dispenser[facing=east]"_s);
        put(4, 0, 6, u"lever[attach=floor]"_s);
        fill(4, 0, 5, {stack("qtcraft:arrow", 1)});
        ticks(2);

        const double before = healthOf(pig);
        flip(4, 0, 6);
        ticks(20);
        QVERIFY(healthOf(pig) < before);
        QVERIFY(!m_server->entities().creatures().find(pig)->effects);
    }

    void aHitOnAPlayerGivesTheEffect()
    {
        const QcVec3 feet = player().motion.position;
        const QcVec3 velocity{60.0, 0.0, 0.0};
        QVERIFY(m_server->systems().projectiles.launch({0, u"qtcraft:weakness_tipped_arrow"_s, feet + QcVec3{-1.5, 0.9, 0.0}, velocity}));
        ticks(2);
        const QList<QcStatusEffect> effects = player().effects.all();
        QCOMPARE(effects.size(), 1);
        QCOMPARE(effects.front().name, u"qtcraft:weakness"_s);
        QVERIFY(effects.front().ticksLeft > 200 && effects.front().ticksLeft <= 225);
    }

    void aHitThatDoesNotCountGivesNoEffect()
    {
        const quint32 pig = pigAt(6, 5);
        QVERIFY(shoot(u"qtcraft:arrow"_s, 3, 5, 60.0));
        ticks(2);

        // Right after the hit it is invulnerable: the tipped arrow bounces off.
        QVERIFY(shoot(u"qtcraft:poison_tipped_arrow"_s, 5, 5, 60.0));
        ticks(1);
        QCOMPARE(projectiles().all().size(), 1);
        QVERIFY(projectiles().all().front().motion.velocity.x < 0.0);
        QCOMPARE(ticksOn(pig, u"qtcraft:poison"_s), 0);
    }

    void harmingAddsToAWeakHitOnly()
    {
        // At 20 m/s the arrow deals 2 and the instant damage of 6 takes the difference to it, as a
        // stronger hit in the short invulnerability does (decision E-1101).
        const quint32 weak = pigAt(6, 5);
        const double before = healthOf(weak);
        QVERIFY(shoot(u"qtcraft:harming_tipped_arrow"_s, 5, 5, 20.0));
        ticks(6);
        QCOMPARE(healthOf(weak), before - 6.0);

        // At 60 m/s the arrow already deals 6: the instant damage adds nothing.
        const quint32 strong = pigAt(6, 8);
        QVERIFY(shoot(u"qtcraft:harming_tipped_arrow"_s, 3, 8, 60.0));
        ticks(6);
        QCOMPARE(healthOf(strong), before - 6.0);
    }

    void harmingIIAtFullSpeedKillsAWoundedCreature()
    {
        // The arrow leaves 4 of 10; the 12 of the effect take the difference to the arrow's 6.
        const quint32 pig = pigAt(6, 5);
        setHealth(pig, 10.0);
        QVERIFY(shoot(u"qtcraft:strong_harming_tipped_arrow"_s, 3, 5, 60.0));
        ticks(6);
        QVERIFY(!m_server->entities().creatures().find(pig));
    }

    void weakHarmingKillsACreatureOfFiveHealth()
    {
        const quint32 pig = pigAt(6, 5);
        setHealth(pig, 5.0);
        QVERIFY(shoot(u"qtcraft:harming_tipped_arrow"_s, 5, 5, 20.0));
        ticks(6);
        QVERIFY(!m_server->entities().creatures().find(pig));
    }

    void aKillByTheEffectCountsForTheShooter()
    {
        const quint32 pig = pigAt(6, 5);
        setHealth(pig, 5.0);
        const QcBlockPos cell = at(5, 0, 5);
        QVERIFY(m_server->systems().projectiles.launch({player().id, u"qtcraft:harming_tipped_arrow"_s, {cell.x + 0.5, cell.y + 0.5, cell.z + 0.5}, {20.0, 0.0, 0.0}}));
        ticks(6);
        QVERIFY(!m_server->entities().creatures().find(pig));
        QVERIFY(m_server->orbs().totalPoints() > 0);
    }

    void aKillByADispensersArrowCountsForNobody()
    {
        const quint32 pig = pigAt(6, 5);
        setHealth(pig, 5.0);
        QVERIFY(shoot(u"qtcraft:harming_tipped_arrow"_s, 5, 5, 20.0));
        ticks(6);
        QVERIFY(!m_server->entities().creatures().find(pig));
        QCOMPARE(m_server->orbs().totalPoints(), 0);
    }

    void harmingReachesAPlayerToo()
    {
        const QcVec3 feet = player().motion.position;
        QVERIFY(m_server->systems().projectiles.launch({0, u"qtcraft:harming_tipped_arrow"_s, feet + QcVec3{-0.9, 0.9, 0.0}, {20.0, 0.0, 0.0}}));
        ticks(6);
        QCOMPARE(player().health, QcServerClient::maxHealth - 6.0);
    }

    void harmingIIKillsAWoundedPlayer()
    {
        player().health = 10.0;
        const QcVec3 feet = player().motion.position;
        QVERIFY(m_server->systems().projectiles.launch({0, u"qtcraft:strong_harming_tipped_arrow"_s, feet + QcVec3{-1.5, 0.9, 0.0}, {60.0, 0.0, 0.0}}));
        ticks(6);
        QVERIFY(player().dead);
    }

    void theUndeadTakeHarmingAsHealingAndHealingAsHarm()
    {
        const auto zombieAt = [this](int z) {
            const QcBlockPos cell = at(6, 0, z);
            const QcVec3 place{cell.x + 0.5, static_cast<double>(cell.y), cell.z + 0.5};
            return m_server->entities().creatures().spawn(*m_content->mobKind(u"qtcraft:zombie"_s), place, m_server->entities().newId());
        };

        // Harming heals 4 after the 6 of the arrow.
        const quint32 healed = zombieAt(5);
        const double before = healthOf(healed);
        QVERIFY(shoot(u"qtcraft:harming_tipped_arrow"_s, 3, 5, 60.0));
        ticks(6);
        QCOMPARE(healthOf(healed), before - 6.0 + 4.0);

        // Healing hurts by 6, which is no stronger than the arrow's 6 at full draw.
        const quint32 full = zombieAt(8);
        QVERIFY(shoot(u"qtcraft:healing_tipped_arrow"_s, 3, 8, 60.0));
        ticks(6);
        QCOMPARE(healthOf(full), before - 6.0);

        // A weak arrow of 2 leaves it the difference: 6 in all.
        const quint32 weak = zombieAt(2);
        QVERIFY(shoot(u"qtcraft:healing_tipped_arrow"_s, 5, 2, 20.0));
        ticks(6);
        QCOMPARE(healthOf(weak), before - 6.0);
    }

    void healingHealsAfterTheHit()
    {
        const quint32 pig = pigAt(6, 5);
        const double before = healthOf(pig);
        QVERIFY(shoot(u"qtcraft:healing_tipped_arrow"_s, 3, 5, 60.0));
        ticks(6);
        QCOMPARE(healthOf(pig), before - 6.0 + 4.0);
    }

    void aStuckArrowComesBackAsItself()
    {
        const int before = player().inventory.countOf(item("qtcraft:speed_tipped_arrow"));
        // Into the ground right beside the player, not through him.
        const QcVec3 beside{m_feet.x + 1.2, m_feet.y + 3.0, m_feet.z + 0.5};
        QVERIFY(m_server->systems().projectiles.launch({0, u"qtcraft:speed_tipped_arrow"_s, beside, {0.0, -30.0, 0.0}}));
        ticks(3);
        QVERIFY(projectiles().all().isEmpty());
        QCOMPARE(player().inventory.countOf(item("qtcraft:speed_tipped_arrow")), before + 1);
        QCOMPARE(player().inventory.countOf(item("qtcraft:arrow")), 0);
    }

    void aCreativeShotDoesNotComeBack()
    {
        const QcVec3 beside{m_feet.x + 1.2, m_feet.y + 3.0, m_feet.z + 0.5};
        QVERIFY(m_server->systems().projectiles.launch({0, u"qtcraft:speed_tipped_arrow"_s, beside, {0.0, -30.0, 0.0}, QcProjectilePickup::Remove}));
        ticks(3);
        QVERIFY(projectiles().all().isEmpty());
        QCOMPARE(player().inventory.countOf(item("qtcraft:speed_tipped_arrow")), 0);
    }

    void aStuckArrowIsSavedWithItsKind()
    {
        const QcBlockPos wall = m_feet.offset(3, 1, 0);
        m_server->world().setBlock(wall, id("qtcraft:stone"));
        QVERIFY(m_server->systems().projectiles.launch({0, u"qtcraft:poison_tipped_arrow"_s, {m_feet.x + 1.5, m_feet.y + 1.5, m_feet.z + 0.5}, {30.0, 0.0, 0.0}}));
        ticks(3);
        QCOMPARE(projectiles().all().size(), 1);
        QVERIFY(projectiles().all().front().stuck);

        QcEntityRecord record;
        record.chunk = m_feet.chunk();
        record.projectiles.append({projectiles().all().front(), projectiles().all().front().id});
        QcLoadWarnings warnings;
        const QcResult<QcEntityRecord> back = QcEntityCodec::decode(QcEntityCodec::encode(record, *m_content), record.chunk, *m_content, warnings);
        QVERIFY(back.has_value());
        QCOMPARE(back->projectiles.size(), 1);
        QCOMPARE(back->projectiles.front().entry.type, u"qtcraft:poison_tipped_arrow"_s);
        QVERIFY(back->projectiles.front().entry.stuck);

        // Unloaded with the chunk and back, it is still that arrow.
        const QHash<QcChunkPos, QcEntityRecord> taken = QcEntityRecords::take(m_server->entities(), {m_feet.chunk()});
        QVERIFY(projectiles().all().isEmpty());
        QVERIFY(QcEntityRecords::apply(taken.value(m_feet.chunk()), m_server->entities()).isEmpty());
        QCOMPARE(projectiles().all().size(), 1);
        QCOMPARE(projectiles().all().front().type, u"qtcraft:poison_tipped_arrow"_s);
    }

    void theyAreNoPotions()
    {
        const QcBrewingRules rules(m_content);
        const QcItemStack arrow = stack("qtcraft:poison_tipped_arrow", 1);
        const QcBrewerDefinition &brewer = *m_content->block(*m_content->blockId(u"qtcraft:brewing_stand"_s)).machine.brewing;
        QVERIFY(!m_content->potionCatalog().potionOf(arrow.item));
        QVERIFY(!rules.accepts(brewer, QcBrewingRules::firstBottleSlot, arrow));
        QVERIFY(!rules.mix(arrow.item, item("qtcraft:gunpowder")));
    }
};

QTEST_GUILESS_MAIN(TestTippedArrows)
#include "test_tipped_arrows.moc"
