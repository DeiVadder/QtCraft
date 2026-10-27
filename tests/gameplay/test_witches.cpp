#include "common/content/parser/QcContentParser.h"
#include "common/content/parser/QcProjectileParser.h"
#include "common/diagnostics/QcThreadCpuClock.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcDayCycle.h"
#include "QcServerTestFixture.h"
#include "server/gameplay/QcServerEffects.h"
#include "server/mobs/QcArcherBrain.h"
#include "server/mobs/QcArcherState.h"

#include <QJsonDocument>
#include <QTest>

using namespace Qt::StringLiterals;

// Witches (decision E-623) and splash potions (decision E-624): a witch is an archer from data that
// throws poison while its target is not poisoned and harming once it is, holds back while it drinks
// a potion of healing when hurt, and has no blows; a splash reaches every living body within its
// radius but the thrower, weaker with the distance, harming through armour. Ticks are stepped by
// hand.
class TestWitches : public QcServerTestFixture
{
    Q_OBJECT

private:
    static constexpr double midnight = QcDayCycle::dayLength * 10.75;
    static constexpr int arena = 16;

    // Projectiles nextShot already returned.
    QSet<quint32> m_seenShots;

    QcServerClient &player() { return *m_server->entities().clients().front(); }
    QcItemStack stack(const char *name, int count) const { return {item(name), count, 0}; }
    int type(const char *name) const { return *m_content->mobIndex(u"qtcraft:"_s + QString::fromLatin1(name)); }
    QcCreature *mob(quint32 id) { return m_server->entities().creatures().find(id); }
    const QcMobDefinition &witchKind() const { return m_content->mobs()[type("witch")]; }

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

    int poisonTicksOf(quint32 creature)
    {
        const QcCreature *found = mob(creature);
        const QcStatusEffect *poison = found && found->effects.has() ? found->effects->find(u"qtcraft:poison"_s) : nullptr;
        return poison ? poison->ticksLeft : 0;
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
        QCOMPARE(witch.archer.choices.size(), 1);
        QCOMPARE(witch.archer.choices.front().projectile, u"qtcraft:poison_splash_potion"_s);
        QCOMPARE(witch.archer.choices.front().unlessTargetHas, u"qtcraft:poison"_s);
        QVERIFY(witch.drinks);
        QCOMPARE(witch.drinks->itemId, item("qtcraft:healing_potion"));
        QVERIFY(witch.immuneTo.contains(u"qtcraft:poison"_s));
        QCOMPARE(witch.attackDamage, 0.0);
        QCOMPARE(witch.spawnWeight, 0.0);
        const QcProjectileDefinition *poison = m_content->projectile(u"qtcraft:poison_splash_potion"_s);
        QVERIFY(poison && poison->breaks());
        QCOMPARE(poison->effect(QcHitEffect::splash)->number, 4.0);

        // A splash breaks the projectile even when the file names no "break".
        const QcResult<QcProjectileDefinition> bare = QcProjectileParser::parseProjectile(
            QJsonDocument::fromJson(R"({"name":"p","item":"p","onHit":[{"type":"splash","radius":2}]})").object(), u"test"_s);
        QVERIFY(bare && bare->breaks());

        // Drinking goes with the archer AI, a choice needs both fields.
        const auto parse = [](const char *json) {
            const QcResult<QcMobDefinition> mob = QcContentParser::parseMob(QJsonDocument::fromJson(json).object(), u"qtcraft"_s);
            if (!mob) {
                qInfo().noquote() << mob.error();
            }
            return mob.has_value();
        };
        QVERIFY(parse(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","hostile":true,"ai":"archer","archer":{"projectile":"arrow","choices":[{"projectile":"snowball","unlessTargetHas":"poison"}]},"drinks":{"item":"milk_bucket"}})"));
        QVERIFY(!parse(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","hostile":true,"drinks":{"item":"milk_bucket"}})"));
        QVERIFY(!parse(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","hostile":true,"ai":"archer","archer":{"projectile":"arrow","choices":[{"projectile":"snowball"}]}})"));
        QVERIFY(!parse(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","hostile":true,"ai":"archer","archer":{"projectile":"arrow"},"drinks":{}})"));
    }

    void choiceFollowsTheTargetsEffects()
    {
        const QcArcherDefinition &archer = witchKind().archer;
        QcMobManager::PlayerView target;
        QCOMPARE(QcArcherBrain::projectileFor(archer, target), u"qtcraft:poison_splash_potion"_s);
        QcStatusEffects effects;
        target.effects = &effects;
        QCOMPARE(QcArcherBrain::projectileFor(archer, target), u"qtcraft:poison_splash_potion"_s);
        QVERIFY(effects.give({u"qtcraft:poison"_s, 1, 100}));
        QCOMPARE(QcArcherBrain::projectileFor(archer, target), u"qtcraft:harming_splash_potion"_s);
    }

    void aWitchPoisonsFirstThenHarms()
    {
        joinArena();
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

        // The player, hit in the middle, gets nearly the whole 20 s, the pig 2 m away less, the
        // thrower and a pig beyond the radius nothing.
        const QcStatusEffect *onPlayer = player().effects.find(u"qtcraft:poison"_s);
        QVERIFY(onPlayer);
        QVERIFY2(onPlayer->ticksLeft > 380 && onPlayer->ticksLeft <= 400, qPrintable(QString::number(onPlayer->ticksLeft)));
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
        // Inside the player's body: the whole 6, armour or not; the pig 2.2 m from its box less.
        QCOMPARE(player().health, before - 6.0);
        QVERIFY(mob(pig)->health < pigBefore && mob(pig)->health > pigBefore - 4.0);
    }

    void aHurtWitchDrinksAndHeals()
    {
        joinArena();
        chat(u"/gamemode creative"_s);
        const quint32 witch = spawn("witch", 0.0, -5.0);
        const QcDrinkHealDefinition &drink = *witchKind().drinks;
        ticks(1);
        QVERIFY(!mob(witch)->archer->drinking());

        // Within a drink of its most it does not drink; below it begins.
        mob(witch)->health = witchKind().maxHealth - drink.heal + 1.0;
        ticks(2);
        QVERIFY(!mob(witch)->archer->drinking());
        mob(witch)->health = 18.0;
        ticks(1);
        QVERIFY(mob(witch)->archer->drinking());
        QCOMPARE(m_client->held.value(witch), drink.itemId);

        // After the drink's time it heals by `heal`, puts the bottle away and waits its cooldown.
        const int drinkTicks = static_cast<int>(std::lround(drink.seconds / QcPhysics::tickDuration));
        ticks(drinkTicks);
        QCOMPARE(mob(witch)->health, 22.0);
        QVERIFY(!mob(witch)->archer->drinking());
        QCOMPARE(m_server->creatures().drinking().drinks(), 1);
        QCOMPARE(m_client->held.value(witch), qcNoItem);
        mob(witch)->health = 10.0;
        const int cooldownTicks = static_cast<int>(std::lround(drink.cooldownSeconds / QcPhysics::tickDuration));
        ticks(cooldownTicks - 2);
        QVERIFY(!mob(witch)->archer->drinking());
        ticks(3);
        QVERIFY(mob(witch)->archer->drinking());
    }

    void aDrinkingWitchThrowsNothing()
    {
        joinArena();
        chat(u"/gamemode survival"_s);
        const quint32 witch = spawn("witch", 0.0, -7.0);
        mob(witch)->health = 5.0;
        ticks(1);
        QVERIFY(mob(witch)->archer->drinking());
        const int drinkTicks = static_cast<int>(std::lround(witchKind().drinks->seconds / QcPhysics::tickDuration));
        for (int tick = 0; tick < drinkTicks - 2; ++tick) {
            ticks(1);
            QVERIFY(m_server->entities().projectiles().all().isEmpty());
            QVERIFY(!mob(witch)->archer->drawing());
        }
    }

    void aWitchHasNoBlows()
    {
        // Next to a creative player she is no threat in melee, and a survival player beside her
        // takes nothing but her potions (none while she is held by a drink).
        joinArena();
        chat(u"/gamemode survival"_s);
        const quint32 witch = spawn("witch", 0.0, -1.0);
        mob(witch)->health = 5.0;
        const double before = player().health;
        ticks(20);
        QCOMPARE(player().health, before);
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
            chat(u"/gamerule naturalRegeneration false"_s);
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
