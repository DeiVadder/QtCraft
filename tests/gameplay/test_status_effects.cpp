#include "common/protocol/QcProtocol.h"
#include "QcServerTestFixture.h"
#include "server/gameplay/QcServerEffects.h"
#include "server/projectiles/QcHitEffects.h"
#include "server/storage/QcEntityCodec.h"
#include "server/storage/QcPlayerStorage.h"

using namespace Qt::StringLiterals;

// Status effects (decision E-450): poison's damage curve and its floor at 1 health on players and
// creatures, immunities, /effect, the HUD's data in the health message (on change and every second),
// the death message of effect damage, Roman level numerals, saving with creatures and players, and
// the "effect" hit of projectiles. Ticks are stepped by hand.
class TestStatusEffects : public QcServerTestFixture
{
    Q_OBJECT

private:
    static inline const QString poison = u"qtcraft:poison"_s;

    QcServerClient &player() { return *m_server->entities().clients().front(); }
    int type(const char *name) const { return *m_content->mobIndex(u"qtcraft:"_s + QString::fromLatin1(name)); }

    // The player in survival with food that neither heals nor starves.
    void joinHungry()
    {
        join();
        chat(u"/gamemode survival"_s);
        ticks(1);
        player().hunger.restore(10, 0.0);
    }

    // Health of the player after `count` ticks of poison at `level` given for `seconds`; -1 when
    // it was not given.
    double healthAfterPoison(int level, int seconds, int count)
    {
        if (!QcServerEffects::givePlayer(*m_content, player(), {poison, level, seconds * 20})) {
            return -1.0;
        }
        ticks(count);
        return player().health;
    }

private slots:
    void poisonHurtsEveryPeriodAndHalvesItPerLevel()
    {
        joinHungry();
        const QcEffectDefinition *definition = m_content->effect(poison);
        QVERIFY(definition);
        QCOMPARE(definition->periodAt(1), 25);
        QCOMPARE(definition->periodAt(2), 12);
        QCOMPARE(definition->periodAt(3), 6);

        // 10 s at level I: hurts with 200, 175, …, 25 ticks left, 8 times.
        QCOMPARE(healthAfterPoison(1, 10, 220), 12.0);
        QVERIFY(player().effects.isEmpty());

        player().health = 20.0;
        // 5 s at level II: 100 ticks left down to 1, every 12th: 8 times.
        QCOMPARE(healthAfterPoison(2, 5, 120), 12.0);
    }

    void poisonNeverKills()
    {
        joinHungry();
        player().health = 3.0;
        QCOMPARE(healthAfterPoison(1, 60, 600), 1.0);
        QVERIFY(!player().dead);
        QVERIFY(player().effects.find(poison));
    }

    // Effect damage goes through the short invulnerability after a hurt like any other damage: a
    // poison hit inside an open window, weaker than the hit that opened it, takes nothing (decisions
    // E-429, E-450).
    void poisonInsideAnOpenHurtWindowTakesNothing()
    {
        joinHungry();
        QCOMPARE(m_server->systems().health.damage(player(), 3.0, {}), QcHurtWindow::Take::Fresh);
        QCOMPARE(player().health, 17.0);
        QVERIFY(player().hurt.active());

        // The first poison hit falls 1 tick after it is given, inside the window of 10 ticks.
        QVERIFY(QcServerEffects::givePlayer(*m_content, player(), {poison, 1, 200}));
        ticks(QcHurtWindow::defaultTicks - 1);
        QCOMPARE(player().health, 17.0);

        // The next one, 25 ticks later, outside the window, takes its point.
        ticks(25);
        QCOMPARE(player().health, 16.0);
    }

    void creaturesArePoisonedUnlessImmune()
    {
        join();
        const QcVec3 at{m_feet.x + 2.5, double(m_feet.y), m_feet.z + 0.5};
        QcMobManager &creatures = m_server->entities().creatures();
        const quint32 pig = creatures.spawn(type("pig"), at, m_server->entities().newId());
        const quint32 zombie = creatures.spawn(type("zombie"), at, m_server->entities().newId());

        QVERIFY(QcServerEffects::giveCreature(*m_content, m_server->entities().creatures(), pig, {poison, 1, 60 * 20}));
        QVERIFY(!QcServerEffects::giveCreature(*m_content, m_server->entities().creatures(), zombie, {poison, 1, 60 * 20}));
        QVERIFY(!creatures.find(zombie)->effects);
        ticks(30 * 20);
        // A pig has 10 health; poison leaves it 1.
        QCOMPARE(creatures.find(pig)->health, 1.0);
        QVERIFY(creatures.find(pig)->effects);
    }

    void strongerAndLongerEffectsReplaceWeakerOnes()
    {
        QcStatusEffects effects;
        QVERIFY(effects.give({poison, 1, 100}));
        QVERIFY(!effects.give({poison, 1, 50}));
        QVERIFY(effects.give({poison, 1, 200}));
        QVERIFY(!effects.give({poison, 1, 10'000'000}));
        QVERIFY(effects.give({poison, 2, 20}));
        QCOMPARE(effects.all().size(), 1);
        QCOMPARE(effects.all().front().level, 2);
        for (int i = 0; i < 19; ++i) {
            QVERIFY(!effects.tick());
        }
        QVERIFY(effects.tick());
        QVERIFY(effects.isEmpty());
    }

    void effectCommandGivesAndClears()
    {
        joinHungry();
        chat(u"/effect give Raw poison 3 2"_s);
        ticks(1);
        const QcStatusEffect *given = player().effects.find(poison);
        QVERIFY(given);
        QCOMPARE(given->level, 2);
        QVERIFY(given->ticksLeft > 55 && given->ticksLeft <= 60);
        // The client hears of it with his health.
        QCOMPARE(m_client->effects.size(), 1);
        QCOMPARE(m_client->effects.front().name, poison);

        chat(u"/effect give Raw nothing"_s);
        QVERIFY(m_client->chatLines.last().contains(u"nothing"_s));
        chat(u"/effect clear Raw"_s);
        ticks(1);
        QVERIFY(player().effects.isEmpty());
        QVERIFY(m_client->effects.isEmpty());
    }

    void healthMessageCarriesEffectsEverySecond()
    {
        joinHungry();
        QVERIFY(QcServerEffects::givePlayer(*m_content, player(), {poison, 1, 100}));
        ticks(1);
        const int first = m_client->effects.front().ticksLeft;
        ticks(20);
        // Within a second the client hears of the time left again; it lags less than a second.
        QVERIFY(m_client->effects.front().ticksLeft < first);
        QVERIFY(m_client->effects.front().ticksLeft - player().effects.find(poison)->ticksLeft < QcServerEffects::sendIntervalTicks);
        ticks(100);
        QVERIFY(m_client->effects.isEmpty());

        // The message round trip keeps them and refuses damaged ones.
        const QcMsg::PlayerHealth sent{12.0, 20.0, false, false, {{poison, 2, 40}}};
        const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(sent), QcProtocol::Peer::Server);
        QVERIFY(decoded);
        QCOMPARE(std::get<QcMsg::PlayerHealth>(*decoded).effects, sent.effects);
        const QcMsg::PlayerHealth damaged{12.0, 20.0, false, false, {{poison, 99, 40}}};
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(damaged), QcProtocol::Peer::Server));
    }

    void effectsAreSentOnChangeNotEveryTick()
    {
        joinHungry();
        QVERIFY(QcServerEffects::givePlayer(*m_content, player(), {poison, 1, 1000}));
        ticks(1);
        QCOMPARE(m_client->effects.size(), 1);

        // Over two seconds: the countdown twice and poison's one or two hurts, not one per tick.
        const int before = m_client->healthMessages;
        ticks(40);
        const int sent = m_client->healthMessages - before;
        QVERIFY2(sent >= 2 && sent <= 4, qPrintable(QString::number(sent)));

        // Given again for longer: at once, with the new time.
        const int beforeLonger = m_client->healthMessages;
        QVERIFY(QcServerEffects::givePlayer(*m_content, player(), {poison, 1, 3000}));
        ticks(1);
        QVERIFY(m_client->healthMessages > beforeLonger);
        QVERIFY(m_client->effects.front().ticksLeft > 2900);

        // A stronger level, then taken away: each at once.
        QVERIFY(QcServerEffects::givePlayer(*m_content, player(), {poison, 2, 100}));
        ticks(1);
        QCOMPARE(m_client->effects.front().level, 2);
        QVERIFY(player().effects.remove(poison));
        ticks(1);
        QVERIFY(m_client->effects.isEmpty());

        // Without effects nothing more is sent.
        const int idle = m_client->healthMessages;
        ticks(40);
        QCOMPARE(m_client->healthMessages, idle);
    }

    void effectDamageHasItsOwnDeathMessage()
    {
        // A mod's poison that kills (no floor).
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcEffectDefinition deadly = *content->effect(poison);
        deadly.name = u"test:deadly"_s;
        deadly.minHealth = 0.0;
        deadly.damage = 50.0;
        content->registerEffect(deadly);
        m_content = content;
        joinHungry();
        QVERIFY(QcServerEffects::givePlayer(*m_content, player(), {deadly.name, 1, 25}));
        ticks(1);
        QVERIFY(player().dead);
        QVERIFY(std::ranges::any_of(m_client->chatKeys, [](const QcMsg::ChatMessage &line) { return line.key == u"%1 died of a status effect"_s; }));
    }

    void levelsShowAsRomanNumerals()
    {
        QCOMPARE(QcStatusEffects::levelName(1), u"I"_s);
        QCOMPARE(QcStatusEffects::levelName(4), u"IV"_s);
        QCOMPARE(QcStatusEffects::levelName(9), u"IX"_s);
        QCOMPARE(QcStatusEffects::levelName(QcEffectDefinition::maxLevel), u"X"_s);
        QCOMPARE(QcStatusEffects::levelName(11), u"11"_s);
    }

    void deathTakesEffectsAway()
    {
        joinHungry();
        QVERIFY(QcServerEffects::givePlayer(*m_content, player(), {poison, 1, 1000}));
        chat(u"/kill"_s);
        ticks(1);
        QVERIFY(player().effects.isEmpty());
        QVERIFY(!QcServerEffects::givePlayer(*m_content, player(), {poison, 1, 1000}));
    }

    void effectsAreSavedWithCreaturesAndPlayers()
    {
        QcEntityRecord record;
        record.chunk = {0, 0};
        QcMobManager::Stored chicken;
        chicken.type = type("pig");
        chicken.position = {3.5, 70.0, 4.5};
        chicken.health = 5.0;
        chicken.uid = 7;
        chicken.effects = {{poison, 2, 333}};
        record.creatures.append({chicken, 1});
        QStringList warnings;
        QSet<QString> reported;
        const QcResult<QcEntityRecord> read =
            QcEntityCodec::decode(QcEntityCodec::encode(record, *m_content), {0, 0}, *m_content, warnings, reported);
        QVERIFY(read);
        QCOMPARE(read->creatures.front().entry.effects, chicken.effects);

        QcPlayerStorage storage(QDir::tempPath(), m_content);
        QcPlayerData data;
        data.motion.position = {1.0, 70.0, 1.0};
        data.effects = {{poison, 1, 400}};
        const QcResult<std::optional<QcPlayerData>> parsed = storage.parse(u"Raw"_s, storage.bytes(data), warnings);
        QVERIFY(parsed && *parsed);
        QCOMPARE((*parsed)->effects, data.effects);
    }

    void effectHitsAddUp()
    {
        QcProjectileDefinition arrow = *m_content->projectile(u"qtcraft:arrow"_s);
        QcHitEffect effect;
        effect.type = QString::fromLatin1(QcHitEffect::effect);
        effect.number = 2.5;
        effect.subject = poison;
        effect.level = 2;
        arrow.onHit.append(effect);
        const QcHitEffects::Impact impact = QcHitEffects().impactOf(arrow, 6.0);
        QCOMPARE(impact.damage, 6.0);
        QCOMPARE(impact.effects, (QList<QcStatusEffect>{{poison, 2, 50}}));
    }

    void spawnHitsBringYoungOnesWhereTheProjectileBreaks()
    {
        // A snowball that brings two young pigs wherever it breaks.
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcProjectileDefinition egg = *content->projectile(u"qtcraft:snowball"_s);
        egg.name = u"test:pig_egg"_s;
        QcHitEffect spawn;
        spawn.type = QString::fromLatin1(QcHitEffect::spawn);
        spawn.number = 1.0;
        spawn.subject = u"qtcraft:pig"_s;
        spawn.count = 2;
        egg.onHit.append(spawn);
        content->registerProjectile(egg);
        m_content = content;
        join();

        const QcVec3 start{m_feet.x + 2.5, m_feet.y + 2.0, m_feet.z + 0.5};
        QVERIFY(m_server->systems().projectiles.launch({0, egg.name, start, {0.0, -10.0, 0.0}}));
        for (int i = 0; i < 40 && !m_server->entities().projectiles().all().isEmpty(); ++i) {
            ticks(1);
        }
        QVERIFY(m_server->entities().projectiles().all().isEmpty());
        const auto young = std::ranges::count_if(m_server->entities().creatures().mobs(), [&](const QcCreature &mob) {
            return mob.type == type("pig") && mob.isYoung() && mob.persistent;
        });
        QCOMPARE(young, 2);
    }

    void effectsReachTheClientContent()
    {
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        m_content->writeTo(out);
        QDataStream in(bytes);
        const QcResult<QcContentRegistry> received = QcContentRegistry::readFrom(in);
        QVERIFY(received);
        const QcEffectDefinition *effect = received->effect(poison);
        QVERIFY(effect);
        QCOMPARE(effect->colour, 0x4e9331u);
        QVERIFY(effect->tintsHearts);
        QCOMPARE(effect->icon, u"qtcraft:spider_eye"_s);
    }
};

QTEST_MAIN(TestStatusEffects)
#include "test_status_effects.moc"
