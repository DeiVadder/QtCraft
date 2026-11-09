// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/entitymeshes/QcEntityMesher.h"
#include "clientcore/light/QcSky.h"
#include "common/content/mobs/QcModelDefinition.h"
#include "common/core/QcWorldConstants.h"
#include "common/gameplay/QcEffectRules.h"
#include "common/gameplay/QcMining.h"
#include "common/gameplay/QcSwingSpeed.h"
#include "common/protocol/QcProtocol.h"
#include "common/world/QcDayCycle.h"
#include "fixtures/QcServerTestFixture.h"
#include "server/gameplay/QcMovementEpochs.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerEffects.h"
#include "server/storage/QcPlayerStorage.h"

using namespace Qt::StringLiterals;

// What every kind of status effect of 85.1 does (decision E-992), one case per rule, through the
// real paths: /effect, inputs of the raw client, blows, blocks, the tick. A case compares with the
// same scene without the effect where the plain outcome is not obvious. The prediction over a slow
// link stands in test_effects_client. Ticks are stepped by hand.
class TestEffectRules : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcServerClient &player() { return *m_server->entities().clients().front(); }

    // A survival player with food that neither heals nor starves on a wide stone floor.
    void stage()
    {
        join();
        for (int dx = -14; dx <= 14; ++dx) {
            for (int dz = -14; dz <= 14; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:stone"));
                for (int dy = 0; dy <= 3; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        chat(u"/gamerule mobSpawning false"_s);
        chat(u"/gamemode survival"_s);
        // By night: the sun burns the undead (QcBodyBurning) and would blur what they are dealt.
        m_server->setWorldTime(QcDayCycle::midnightTime);
        ticks(1);
        player().hunger.restore(10, 0.0, 0.0);
        idle(2);
    }

    // Gives the raw player `name` by /effect and steps the tick that tells him.
    void give(const QString &name, int seconds = 60, int level = 1)
    {
        chat(u"/effect give Raw %1 %2 %3"_s.arg(name).arg(seconds).arg(level));
        ticks(1);
        QcTestUtil::pump();
    }

    // The count of the effects the client knows.
    quint16 knownEpoch() const { return m_client->healths.isEmpty() ? quint16{0} : m_client->healths.constLast().movementEpoch; }

    // One input into the server, stepped at once, quoting `epoch` (default: the count the client
    // knows).
    void input(QcPlayerInput intent, std::optional<quint16> epoch = {})
    {
        intent.sequence = ++m_sequence;
        intent.movementEpoch = epoch.value_or(knownEpoch());
        m_client->connection.send(QcMsg::PlayerInput{intent});
        QcTestUtil::pump();
        ticks(1);
    }

    // Walks from a stand and returns the speed reached.
    double walkedSpeed()
    {
        player().motion.position = {m_feet.x + 0.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        player().motion.velocity = {};
        for (int tick = 0; tick < 16; ++tick) {
            input({.forward = 1.0f});
        }
        return std::hypot(player().motion.velocity.x, player().motion.velocity.z);
    }

    // Jumps and returns how high the feet rise.
    double jumpHeight()
    {
        const double floor = player().motion.position.y;
        double highest = floor;
        input({.jump = true});
        for (int tick = 0; tick < 40; ++tick) {
            highest = std::max(highest, player().motion.position.y);
            input({});
        }
        return highest - floor;
    }

    // Lets the player fall from `height` blocks above the floor with inputs quoting `epoch`
    // (default: the count the client knows); returns the health lost.
    double fallDamage(double height, std::optional<quint16> epoch = {})
    {
        const double before = player().health;
        player().motion.position.y = m_feet.y + height;
        player().fallPeak = m_feet.y + height;
        player().motion.velocity = {};
        player().motion.onGround = false;
        for (int tick = 0; tick < 240 && !player().motion.onGround; ++tick) {
            input({}, epoch);
        }
        return before - player().health;
    }

    // A creature of `name` without a mind, `away` blocks east of the player.
    quint32 creature(const char *name, double away = 1.5)
    {
        const QcVec3 at{m_feet.x + 0.5 + away, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        const quint32 mob = m_server->mobs().spawn(*m_content->mobKind(u"qtcraft:"_s + QString::fromLatin1(name)), at, m_server->entities().newId(), false, true);
        m_server->mobs().find(mob)->noAi = true;
        return mob;
    }

    bool giveMob(quint32 mob, const char *name, int level = 1)
    {
        return QcServerEffects::giveCreature(*m_content, m_server->mobs(), mob, {u"qtcraft:"_s + QString::fromLatin1(name), level, 60 * 20});
    }

    // The health the fist leaves a pig with after one blow.
    double pigAfterBlow(const char *effect)
    {
        const quint32 pig = creature("pig");
        if (effect) {
            give(QString::fromLatin1(effect));
        }
        player().attackCooldown = 0.0;
        send(QcMsg::AttackEntity{pig});
        const double health = m_server->mobs().find(pig)->health;
        m_server->mobs().remove(pig);
        return health;
    }

    // Whether a zombie `away` blocks from the player chases him after a few seconds. It holds its
    // blow: one that strikes before it looks throws the player out of an invisible one's sight.
    bool chasedBy(double away)
    {
        const quint32 zombie = creature("zombie", away);
        m_server->mobs().find(zombie)->noAi = false;
        m_server->mobs().find(zombie)->attackCooldown = 60.0;
        bool chased = false;
        for (int tick = 0; tick < 60 && !chased; ++tick) {
            idle(1);
            chased = m_server->mobs().find(zombie)->chasing == player().id;
        }
        m_server->mobs().remove(zombie);
        return chased;
    }

    // The block under the seat of the player's feet, as the floor he stands on.
    void floorOf(const char *block) { m_server->world().setBlock(m_feet.offset(0, -1, 0), id(block)); }

    // The health the player keeps in `ticks` in `block`, placed at his feet.
    double healthIn(const char *block, int count)
    {
        m_server->world().setBlock(m_feet, id(block));
        player().motion.position = {m_feet.x + 0.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        ticks(count);
        m_server->world().setBlock(m_feet, qcAirBlockId);
        return player().health;
    }

private slots:
    void everyKindLoadsWithItsNameAndTexts()
    {
        const QStringList kinds{u"speed"_s, u"strength"_s, u"weakness"_s, u"regeneration"_s, u"instant_health"_s, u"instant_damage"_s,
                                u"fire_resistance"_s, u"water_breathing"_s, u"night_vision"_s, u"invisibility"_s, u"jump_boost"_s,
                                u"slow_falling"_s, u"haste"_s, u"resistance"_s, u"wither"_s, u"absorption"_s,
                                u"saturation"_s};
        for (const QString &kind : kinds) {
            const QcEffectDefinition *effect = m_content->effect(u"qtcraft:"_s + kind);
            QVERIFY2(effect, qPrintable(kind));
            for (const QString &language : {u"en"_s, u"de"_s}) {
                const QString text = m_content->translations().translate(effect->translationKey, language);
                QVERIFY2(text != effect->translationKey, qPrintable(kind + u" in "_s + language));
            }
            QVERIFY2(m_content->itemId(effect->icon).has_value(), qPrintable(kind));
        }
    }

    void speedRaisesTheWalkAndLevelsAddUp()
    {
        stage();
        const double plain = walkedSpeed();
        give(u"speed"_s, 60, 2);
        const double fast = walkedSpeed();
        QVERIFY2(std::abs(fast - plain * 1.4) < plain * 0.05, qPrintable(u"plain %1, with speed II %2"_s.arg(plain).arg(fast)));
    }

    void strengthAddsToABlow()
    {
        stage();
        QCOMPARE(pigAfterBlow(nullptr), 9.0);
        QCOMPARE(pigAfterBlow("strength"), 6.0);
    }

    void weaknessTakesFromABlowDownToNothing()
    {
        stage();
        QCOMPARE(pigAfterBlow("weakness"), 10.0);
    }

    void aCreaturesStrengthAddsToItsBlow()
    {
        stage();
        const quint32 zombie = creature("zombie");
        m_server->mobs().find(zombie)->noAi = false;
        QVERIFY(giveMob(zombie, "strength", 2));
        for (int tick = 0; tick < 200 && player().health == 20.0; ++tick) {
            idle(1);
        }
        const double blow = m_content->mobs()[m_server->mobs().find(zombie)->type].attackDamage;
        QCOMPARE(20.0 - player().health, blow + 6.0);
    }

    void regenerationHealsPlayersAndCreaturesEveryPeriod()
    {
        stage();
        player().health = 10.0;
        // 5 s at level I: with 100 and 50 ticks left.
        give(u"regeneration"_s, 5);
        ticks(120);
        QCOMPARE(player().health, 12.0);
        QVERIFY(player().effects.isEmpty());

        // Level II acts twice as often and stops at the full health.
        player().health = 19.5;
        give(u"regeneration"_s, 5, 2);
        ticks(30);
        QCOMPARE(player().health, 20.0);

        const quint32 pig = creature("pig");
        m_server->mobs().find(pig)->health = 5.0;
        QVERIFY(giveMob(pig, "regeneration"));
        ticks(60);
        QVERIFY2(m_server->mobs().find(pig)->health > 5.0, "the pig healed");
    }

    void undeadDoNotRegenerate()
    {
        stage();
        QVERIFY(!giveMob(creature("zombie"), "regeneration"));
    }

    void instantHealthActsOnceDoubledPerLevel()
    {
        stage();
        player().health = 10.0;
        give(u"instant_health"_s, 60);
        ticks(2);
        QCOMPARE(player().health, 14.0);
        QVERIFY(player().effects.isEmpty());
        ticks(40);
        QCOMPARE(player().health, 14.0);

        give(u"instant_health"_s, 60, 2);
        ticks(2);
        QCOMPARE(player().health, 20.0);
    }

    void instantDamageActsOnceDoubledPerLevel()
    {
        stage();
        give(u"instant_damage"_s, 60);
        ticks(2);
        QCOMPARE(player().health, 14.0);
        QVERIFY(player().effects.isEmpty());

        ticks(40);
        player().health = 20.0;
        give(u"instant_damage"_s, 60, 2);
        ticks(2);
        QCOMPARE(player().health, 8.0);
    }

    void instantEffectsAreReversedOnTheUndead()
    {
        stage();
        const quint32 zombie = creature("zombie");
        const double full = m_server->mobs().find(zombie)->health;
        QVERIFY(giveMob(zombie, "instant_health"));
        ticks(2);
        QCOMPARE(m_server->mobs().find(zombie)->health, full - 6.0);
        ticks(30);
        QVERIFY(giveMob(zombie, "instant_damage"));
        ticks(2);
        QCOMPARE(m_server->mobs().find(zombie)->health, full - 2.0);
        // The full amount heals, also beyond the health the creature has.
        ticks(30);
        m_server->mobs().find(zombie)->health = 3.0;
        QVERIFY(giveMob(zombie, "instant_damage", 2));
        ticks(2);
        QCOMPARE(m_server->mobs().find(zombie)->health, std::min(11.0, full));

        const quint32 pig = creature("pig", -1.5);
        QVERIFY(giveMob(pig, "instant_damage"));
        ticks(2);
        QCOMPARE(m_server->mobs().find(pig)->health, 4.0);
        QVERIFY(giveMob(pig, "instant_health"));
        ticks(2);
        QCOMPARE(m_server->mobs().find(pig)->health, 8.0);
    }

    void fireResistanceKeepsOffFlamesLavaAndMagma()
    {
        stage();
        // The scenes hurt without the effect.
        QVERIFY(healthIn("qtcraft:lava", 30) < 20.0);
        player().health = 20.0;
        player().burnTicks = 0;
        ticks(30);
        give(u"fire_resistance"_s);
        QCOMPARE(healthIn("qtcraft:lava", 40), 20.0);
        QCOMPARE(healthIn("qtcraft:fire", 40), 20.0);
        player().burnTicks = 100;
        ticks(40);
        QCOMPARE(player().health, 20.0);
    }

    void fireResistanceKeepsOffMagmaUnderTheFeet()
    {
        stage();
        floorOf("qtcraft:magma_block");
        ticks(40);
        QVERIFY2(player().health < 20.0, "magma hurts");
        give(u"fire_resistance"_s);
        ticks(30);
        player().health = 20.0;
        ticks(40);
        QCOMPARE(player().health, 20.0);
    }

    void fireResistanceProtectsCreaturesToo()
    {
        stage();
        const quint32 pig = creature("pig");
        QVERIFY(giveMob(pig, "fire_resistance"));
        m_server->mobs().find(pig)->burnTicks = 100;
        const quint32 other = creature("pig", -1.5);
        m_server->mobs().find(other)->burnTicks = 100;
        ticks(40);
        QCOMPARE(m_server->mobs().find(pig)->health, 10.0);
        QVERIFY(m_server->mobs().find(other)->health < 10.0);
    }

    void waterBreathingKeepsTheBreath()
    {
        stage();
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dy = 0; dy <= 2; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), id("qtcraft:glass"));
                }
            }
        }
        m_server->world().setBlock(m_feet, id("qtcraft:water"));
        m_server->world().setBlock(m_feet.offset(0, 1, 0), id("qtcraft:water"));
        ticks(40);
        QVERIFY2(player().air.air() < QcAirSupply::maxAir, "the breath runs out without the effect");

        give(u"water_breathing"_s);
        ticks(40);
        QCOMPARE(player().air.air(), QcAirSupply::maxAir);
    }

    void nightVisionBrightensOnlyThoseWhoHaveIt()
    {
        QVERIFY(QcEffectRules::nightVision(*m_content, {{u"qtcraft:night_vision"_s, 1, 100}}));
        QVERIFY(!QcEffectRules::nightVision(*m_content, {{u"qtcraft:speed"_s, 1, 100}}));
        const QcSkyState night = QcSky::at(0.75);
        QCOMPARE(QcSky::withNightVision(night, false).flashLight, night.flashLight);
        QVERIFY(QcSky::withNightVision(night, true).flashLight > 0.5);
        // A stronger blast's flash stays.
        QcSkyState blast = night;
        blast.flashLight = 0.9;
        QCOMPARE(QcSky::withNightVision(blast, true).flashLight, 0.9);
    }

    void invisibilityHidesThePlayerFromCreatureSight()
    {
        stage();
        QVERIFY(chasedBy(8.0));
        give(u"invisibility"_s);
        QVERIFY(!chasedBy(8.0));
        // Close by they still notice him: within 7 % of 20 blocks.
        QVERIFY(chasedBy(1.0));
    }

    void armourGivesAnInvisiblePlayerAway()
    {
        stage();
        give(u"invisibility"_s);
        for (QcItemStack &stack : player().equipment.stacks) {
            stack = {item("qtcraft:iron_helmet"), 1};
        }
        QVERIFY(chasedBy(8.0));
        const QList<QcStatusEffect> effects{{u"qtcraft:invisibility"_s, 1, 100}};
        QCOMPARE(QcEffectRules::sightShare(*m_content, effects, 4), 0.7);
        QCOMPARE(QcEffectRules::sightShare(*m_content, effects, 0), 0.07);
        QCOMPARE(QcEffectRules::sightShare(*m_content, {}, 0), 1.0);
    }

    void anInvisibleFigureKeepsWhatItWearsAndHolds()
    {
        const QcModelDefinition *model = m_content->assets().model(u"qtcraft:humanoid"_s);
        const QcAppearanceDefinition *outfit = m_content->assets().appearance(u"qtcraft:zombie"_s);
        QVERIFY(model && outfit);
        const QcEntityMesher::ModelLook look{model, outfit, nullptr, {}, 1.0};
        const QcEntityMesher::UvLookup uv = [](const QString &) { return QRectF(0.0, 0.0, 1.0, 1.0); };
        const QcAnimator::BodyPose pose = QcAnimator::animateBody({}, 0.0);
        QcEntityMesh plain;
        QcEntityMesher::appendModel(plain, {look, nullptr}, pose, {}, uv);
        QVERIFY(!plain.vertices.isEmpty());
        QcEntityMesh hidden;
        QcEntityMesher::appendModel(hidden, {look, nullptr, nullptr, 0, true}, pose, {}, uv);
        QVERIFY(hidden.vertices.isEmpty());

        // What it wears stays, as much as on a visible body.
        const QList<QcEntityMesher::Attachment> armour{{QcBoneRole::Body, {{-0.3, 0.0, -0.2}, {0.3, 0.6, 0.2}}, u"iron_block"_s, {}}};
        QcEntityMesh dressed;
        QcEntityMesher::appendModel(dressed, {look, nullptr, &armour, 0, true}, pose, {}, uv);
        QcEntityMesh plainDressed;
        QcEntityMesher::appendModel(plainDressed, {look, nullptr, &armour}, pose, {}, uv);
        QVERIFY(!dressed.vertices.isEmpty());
        QCOMPARE(dressed.vertices.size(), plainDressed.vertices.size() - plain.vertices.size());
    }

    void jumpBoostJumpsHigherAndForgivesAFall()
    {
        stage();
        const double plain = jumpHeight();
        const double plainFall = fallDamage(6.0);
        QCOMPARE(plainFall, 3.0);
        player().health = 20.0;
        ticks(20);
        give(u"jump_boost"_s, 60, 2);
        const double high = jumpHeight();
        // Velocity 1.48 times the plain one: height 2.19 times.
        QVERIFY2(std::abs(high / plain - 2.19) < 0.15, qPrintable(u"%1 against %2"_s.arg(high).arg(plain)));
        QCOMPARE(fallDamage(6.0), 1.0);
    }

    void slowFallingHoldsTheFallAndTakesTheDamage()
    {
        stage();
        give(u"slow_falling"_s);
        player().motion.position.y = m_feet.y + 3.0;
        player().motion.velocity = {};
        player().motion.onGround = false;
        input({});
        for (int tick = 0; tick < 20; ++tick) {
            input({});
            QVERIFY2(player().motion.velocity.y >= -2.0, qPrintable(u"%1 m/s"_s.arg(player().motion.velocity.y)));
        }
        // Twelve blocks cost nine health without it; the effect lands him in about six seconds.
        QCOMPARE(fallDamage(12.0), 0.0);
        QVERIFY(player().motion.onGround);
    }

    void slowFallingSparesOnlyTheFallItHolds()
    {
        stage();
        give(u"slow_falling"_s);
        // Inputs still quoting the count before the effect fall at full speed: the fall hurts.
        const quint16 before = static_cast<quint16>(knownEpoch() - 1);
        QCOMPARE(fallDamage(12.0, before), 9.0);
        // A limit as fast as a free fall from the harmless height does not take the damage.
        QVERIFY(QcPlayerHealth::fallHeldBack(2.0f));
        QVERIFY(!QcPlayerHealth::fallHeldBack(0.0f));
        QVERIFY(!QcPlayerHealth::fallHeldBack(13.0f));
        QVERIFY(!QcPlayerHealth::fallHeldBack(static_cast<float>(QcPhysics::terminalVelocity)));
    }

    void hasteShortensMiningOnTheServer()
    {
        stage();
        const QcBlockPos block = m_feet.offset(1, 0, 0);
        m_server->world().setBlock(block, id("qtcraft:stone"));
        const QcBlockDefinition &stone = m_content->block(id("qtcraft:stone"));
        const double seconds = QcMining::breakSeconds(stone, nullptr);
        const double hastened = seconds / 1.4;
        const int ticksNeeded = static_cast<int>(std::ceil(hastened * QcPhysics::tickRate)) + 2;

        // Without the effect this time is too short (and beyond the tolerance of the check).
        QVERIFY(seconds - hastened > 0.5);
        send(QcMsg::StartMining{block});
        ticks(ticksNeeded);
        send(QcMsg::FinishMining{block});
        QCOMPARE(*m_server->world().blockAt(block), id("qtcraft:stone"));

        give(u"haste"_s, 60, 2);
        send(QcMsg::StartMining{block});
        ticks(ticksNeeded);
        send(QcMsg::FinishMining{block});
        QCOMPARE(*m_server->world().blockAt(block), qcAirBlockId);
        // The arm swings faster too (decision E-881), from the same effect.
        QCOMPARE(QcSwingSpeed::of(player().effects.all()).ticks(), QcSwingSpeed::baseTicks - 2);
    }

    void hasteThatEndsWhileMiningDoesNotRefuseTheBlock()
    {
        stage();
        const QcBlockPos block = m_feet.offset(1, 0, 0);
        m_server->world().setBlock(block, id("qtcraft:stone"));
        const double hastened = QcMining::breakSeconds(m_content->block(id("qtcraft:stone")), nullptr) / 1.2;
        give(u"haste"_s, 1);
        send(QcMsg::StartMining{block});
        ticks(static_cast<int>(std::ceil(hastened * QcPhysics::tickRate)) + 2);
        QVERIFY(player().effects.isEmpty());
        send(QcMsg::FinishMining{block});
        QCOMPARE(*m_server->world().blockAt(block), qcAirBlockId);
    }

    void resistanceTakesItsShareButNotTheVoid()
    {
        stage();
        give(u"resistance"_s, 60, 2);
        give(u"instant_damage"_s);
        ticks(2);
        QCOMPARE(player().health, 16.4);

        // Level V stops everything, /kill still kills.
        ticks(30);
        player().health = 20.0;
        give(u"resistance"_s, 60, 5);
        give(u"instant_damage"_s);
        ticks(2);
        QCOMPARE(player().health, 20.0);
        // The void hurts through it, and /kill still kills.
        player().motion.position.y = QcWorld::voidLimitY - 10.0;
        ticks(1);
        QCOMPARE(player().health, 20.0 - QcPlayerHealth::voidDamage);
        chat(u"/kill Raw"_s);
        ticks(1);
        QVERIFY(player().dead);
    }

    void resistanceProtectsCreatures()
    {
        stage();
        const quint32 pig = creature("pig");
        QVERIFY(giveMob(pig, "resistance", 5));
        player().attackCooldown = 0.0;
        send(QcMsg::AttackEntity{pig});
        QCOMPARE(m_server->mobs().find(pig)->health, 10.0);
    }

    void witherHurtsPastTheLastHeartAndTintsThem()
    {
        stage();
        player().health = 3.0;
        give(u"wither"_s, 60);
        QVERIFY(m_content->effect(u"qtcraft:wither"_s)->tintsHearts);
        ticks(170);
        QVERIFY2(player().dead, qPrintable(u"health %1"_s.arg(player().health)));
    }

    void absorptionAddsHealthThatHitsTakeFirst()
    {
        stage();
        give(u"absorption"_s, 60, 2);
        QCOMPARE(player().absorption, 8.0);
        QCOMPARE(m_client->healths.constLast().absorption, 8.0);

        give(u"instant_damage"_s);
        ticks(2);
        QcTestUtil::pump();
        QCOMPARE(player().absorption, 2.0);
        QCOMPARE(player().health, 20.0);
        QCOMPARE(m_client->healths.constLast().absorption, 2.0);
    }

    // Resistance takes its share of a blow first, absorption the rest.
    void resistanceComesBeforeAbsorption()
    {
        stage();
        give(u"resistance"_s, 60, 2);
        give(u"absorption"_s, 60, 2);
        give(u"instant_damage"_s);
        ticks(2);
        QCOMPARE(player().absorption, 4.4);
        QCOMPARE(player().health, 20.0);
    }

    void aUsedUpAbsorptionEndsTheEffect()
    {
        stage();
        give(u"absorption"_s);
        give(u"instant_damage"_s);
        ticks(2);
        QCOMPARE(player().absorption, 0.0);
        QCOMPARE(player().health, 18.0);
        QVERIFY(!player().effects.find(u"qtcraft:absorption"_s));
    }

    void anEndedAbsorptionLeavesNoExtraHealth()
    {
        stage();
        give(u"absorption"_s, 1);
        QCOMPARE(player().absorption, 4.0);
        ticks(25);
        QVERIFY(player().effects.isEmpty());
        QCOMPARE(player().absorption, 0.0);

        give(u"absorption"_s);
        chat(u"/effect clear Raw"_s);
        ticks(2);
        QCOMPARE(player().absorption, 0.0);
    }

    void effectsAndExtraHealthSaveAndLoad()
    {
        QcPlayerStorage storage(QDir::tempPath(), m_content);
        QcPlayerData data;
        data.motion.position = {1.0, 70.0, 1.0};
        data.effects = {{u"qtcraft:absorption"_s, 2, 400}, {u"qtcraft:night_vision"_s, 1, 1000}};
        data.absorption = 3.5;
        const QcPlayerStorage::Result parsed = storage.parse(u"Raw"_s, storage.bytes(data));
        QVERIFY(parsed && parsed->player);
        QCOMPARE(parsed->player->effects, data.effects);
        QCOMPARE(parsed->player->absorption, 3.5);

        data.absorption = -1.0;
        const QcPlayerStorage::Result refused = storage.parse(u"Raw"_s, storage.bytes(data));
        QVERIFY(!refused || !refused->player);
    }

    void movementEpochsKeepOldCountsForTheirGrace()
    {
        QcMovementEpochs epochs;
        QCOMPARE(epochs.epoch(), quint16{0});
        const QcMovementEffects slow{.speed = 0.5f};
        epochs.update({}, 10);
        QCOMPARE(epochs.epoch(), quint16{0});
        epochs.update(slow, 10);
        QCOMPARE(epochs.epoch(), quint16{1});

        // The input in flight moves with what it was predicted with, the next ones with the new.
        QCOMPARE(epochs.forInput(0, 11), QcMovementEffects{});
        QCOMPARE(epochs.forInput(1, 11), slow);
        // A count the server never gave and one past its grace mean the current effects.
        QCOMPARE(epochs.forInput(77, 11), slow);
        QCOMPARE(epochs.forInput(0, 10 + QcMovementEpochs::graceTicks), QcMovementEffects{});
        QCOMPARE(epochs.forInput(0, 11 + QcMovementEpochs::graceTicks), slow);
        // After a death, a respawn or a teleport no old count is good any more.
        epochs.forgetPast();
        QCOMPARE(epochs.forInput(0, 11), slow);
    }

    void aServerStepsAnInputWithTheEffectsItQuotes()
    {
        stage();
        give(u"speed"_s, 60, 4);
        const quint16 before = m_client->healths.constLast().movementEpoch;
        QVERIFY(before > 0);
        player().motion.velocity = {};
        // Still quoting the count before the effect: walks like before it, as the client predicted.
        for (int tick = 0; tick < 16; ++tick) {
            QcPlayerInput intent{.forward = 1.0f, .movementEpoch = static_cast<quint16>(before - 1)};
            intent.sequence = ++m_sequence;
            m_client->connection.send(QcMsg::PlayerInput{intent});
            QcTestUtil::pump();
            ticks(1);
        }
        const double quoted = std::hypot(player().motion.velocity.x, player().motion.velocity.z);
        QVERIFY2(std::abs(quoted - QcPhysics::walkSpeed) < 0.3, qPrintable(u"%1 m/s"_s.arg(quoted)));
    }

    void theWireCarriesCountAndExtraHealth()
    {
        QCOMPARE(QcProtocol::version, 96u);
        QcMsg::PlayerHealth health;
        health.absorption = 6.5;
        health.movementEpoch = 300;
        const QcResult<QcMessage> healthRead = QcProtocol::decode(QcProtocol::encode(QcMessage(health)), QcProtocol::Peer::Server);
        QVERIFY(healthRead);
        QCOMPARE(std::get<QcMsg::PlayerHealth>(*healthRead).absorption, 6.5);
        QCOMPARE(std::get<QcMsg::PlayerHealth>(*healthRead).movementEpoch, quint16{300});
        health.absorption = -1.0;
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMessage(health)), QcProtocol::Peer::Server));
        health.absorption = 20000.0;
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMessage(health)), QcProtocol::Peer::Server));

        QcPlayerInput intent;
        intent.movementEpoch = 7;
        const QcResult<QcMessage> inputRead = QcProtocol::decode(QcProtocol::encode(QcMessage(QcMsg::PlayerInput{intent})), QcProtocol::Peer::Client);
        QVERIFY(inputRead);
        QCOMPARE(std::get<QcMsg::PlayerInput>(*inputRead).input.movementEpoch, quint16{7});
    }

    void theClientGetsWhatItSteppedAndDrawsWith()
    {
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        m_content->writeTo(out);
        QDataStream in(bytes);
        const QcResult<QcContentRegistry> received = QcContentRegistry::readFrom(in);
        QVERIFY(received);
        const QList<QcStatusEffect> effects{{u"qtcraft:jump_boost"_s, 2, 100}, {u"qtcraft:slow_falling"_s, 1, 100}, {u"qtcraft:speed"_s, 1, 100}};
        QCOMPARE(QcEffectRules::movement(*received, effects), QcEffectRules::movement(*m_content, effects));
        QCOMPARE(QcEffectRules::movement(*m_content, effects).maxFallSpeed, 2.0f);
        const QList<QcStatusEffect> soaring{{u"qtcraft:jump_boost"_s, 100, 100}};
        QCOMPARE(QcEffectRules::movement(*m_content, soaring).jump, static_cast<float>(QcEffectRules::maxJumpFactor));
        const QList<QcStatusEffect> others{{u"qtcraft:night_vision"_s, 1, 100}, {u"qtcraft:invisibility"_s, 1, 100}, {u"qtcraft:haste"_s, 1, 100}};
        QVERIFY(QcEffectRules::nightVision(*received, others));
        QVERIFY(QcEffectRules::invisible(*received, others));
        QCOMPARE(QcEffectRules::miningFactor(*received, others), 1.2);
        QVERIFY(received->effect(u"qtcraft:instant_damage"_s)->instant);
    }

    void theEffectCommandGivesAndTakesTheNewKinds()
    {
        stage();
        chat(u"/effect give Raw night_vision 30 1"_s);
        ticks(1);
        QVERIFY(player().effects.find(u"qtcraft:night_vision"_s));
        chat(u"/effect clear Raw night_vision"_s);
        ticks(1);
        QVERIFY(player().effects.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestEffectRules)
#include "test_effect_rules.moc"
