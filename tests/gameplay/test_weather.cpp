#include "QcServerTestFixture.h"

#include "QcTestUtil.h"
#include "server/weather/QcWeatherCycle.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// The weather on the server (stage 35.1, decision E-395): its course is a pure function of the seed;
// /weather sets it with and without a duration; the game rule weatherCycle holds it; it is saved
// with the world and sent to every player, also to one who joins later; rain or snow falls by the
// climate and the height, only under the open sky; lightning strikes only in a thunderstorm and only
// into rain. Ticks are stepped by hand.
class TestWeather : public QcServerTestFixture
{
    Q_OBJECT

private:
    const QcWeatherState &weather() const { return m_server->systems().weather.state(); }

    // Ticks until the strengths of a newly set weather have fully faded in or out.
    void fade() { ticks(QcWeatherCycle::fadeTicks + 2); }

    // The kinds and lengths of the first `spells` spells of a world with `seed`, as the cycle runs.
    static QList<std::pair<QcWeatherKind, qint64>> course(qint64 seed, int spells)
    {
        QList<std::pair<QcWeatherKind, qint64>> result;
        QcWeatherState state;
        QcWeatherCycle::step(state, seed, true);
        result.append({state.kind, state.ticksLeft + 1});
        while (result.size() < spells) {
            const qint64 spell = state.spell;
            QcWeatherCycle::step(state, seed, true);
            if (state.spell != spell) {
                result.append({state.kind, state.ticksLeft + 1});
            }
        }
        return result;
    }

    // Another raw client joining the running server.
    std::unique_ptr<RawClient> joinSecond(const QString &name)
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        auto client = std::make_unique<RawClient>(std::move(clientEnd));
        client->hello(name);
        QcTestUtil::pump();
        ticks(2);
        return client;
    }

private slots:
    void courseFollowsTheSeed()
    {
        const QList<std::pair<QcWeatherKind, qint64>> first = course(42, 400);
        QCOMPARE(course(42, 400), first);
        QVERIFY(course(43, 400) != first);

        // Clear spells alternate with rain or thunder, each within its lengths.
        int thunder = 0;
        for (qsizetype i = 0; i < first.size(); ++i) {
            const auto [kind, length] = first[i];
            QCOMPARE(kind == QcWeatherKind::Clear, i % 2 == 0);
            if (kind == QcWeatherKind::Clear) {
                QVERIFY(length >= QcWeatherCycle::minClearTicks && length <= QcWeatherCycle::maxClearTicks);
            } else if (kind == QcWeatherKind::Rain) {
                QVERIFY(length >= QcWeatherCycle::minRainTicks && length <= QcWeatherCycle::maxRainTicks);
            } else {
                QVERIFY(length >= QcWeatherCycle::minThunderTicks && length <= QcWeatherCycle::maxThunderTicks);
                ++thunder;
            }
        }
        // 200 wet spells at 25 % thunder (seed 42: 45).
        QVERIFY2(thunder >= 30 && thunder <= 70, qPrintable(QString::number(thunder)));
    }

    void strengthsFadeInAndOut()
    {
        QcWeatherState state;
        QcWeatherCycle::step(state, 7, false);
        QcWeatherCycle::set(state, 7, QcWeatherKind::Thunder, 1000);
        for (int i = 0; i < QcWeatherCycle::fadeTicks - 1; ++i) {
            QcWeatherCycle::step(state, 7, false);
        }
        QVERIFY(state.rain > 0.9f && state.rain < 1.0f);
        QcWeatherCycle::step(state, 7, false);
        QCOMPARE(state.rain, 1.0f);
        QCOMPARE(state.thunder, 1.0f);

        QcWeatherCycle::set(state, 7, QcWeatherKind::Rain, 1000);
        for (int i = 0; i < QcWeatherCycle::fadeTicks; ++i) {
            QcWeatherCycle::step(state, 7, false);
        }
        QCOMPARE(state.rain, 1.0f);
        QCOMPARE(state.thunder, 0.0f);
    }

    void jsonKeepsTheStateAndRefusesNonsense()
    {
        QcWeatherState state{QcWeatherKind::Thunder, 1234, 9, 0.5f, 0.25f, 987654};
        QCOMPARE(QcWeatherCycle::fromJson(QcWeatherCycle::toJson(state)), state);

        // Unknown kinds and spells read as a world whose first spell has not begun; strengths clamp.
        QCOMPARE(QcWeatherCycle::fromJson(QJsonObject{{u"kind"_s, u"hail"_s}, {u"spell"_s, 3}}), QcWeatherState{});
        const QcWeatherState clamped =
            QcWeatherCycle::fromJson(QJsonObject{{u"kind"_s, u"rain"_s}, {u"spell"_s, 2}, {u"rain"_s, 7.0}, {u"thunder"_s, -1.0}, {u"ticksLeft"_s, -5}, {u"ticks"_s, -3}});
        QCOMPARE(clamped.rain, 1.0f);
        QCOMPARE(clamped.ticks, 0);
        QCOMPARE(clamped.thunder, 0.0f);
        QCOMPARE(clamped.ticksLeft, 1);
    }

    void weatherCommandWithAndWithoutDuration()
    {
        join();
        QCOMPARE(weather().kind, QcWeatherKind::Clear);

        chat(u"/weather rain 30"_s);
        QCOMPARE(weather().kind, QcWeatherKind::Rain);
        QCOMPARE(weather().ticksLeft, 600);
        QVERIFY(m_client->chatLines.last().contains(u"30"_s));

        // The rain runs out after its 30 s and a clear spell follows (the cycle is on).
        ticks(599);
        QCOMPARE(weather().kind, QcWeatherKind::Rain);
        ticks(1);
        QCOMPARE(weather().kind, QcWeatherKind::Clear);

        // Without a duration the spell lasts as long as the seed says for its number.
        const qint64 spell = weather().spell;
        chat(u"/weather thunder"_s);
        QCOMPARE(weather().kind, QcWeatherKind::Thunder);
        QCOMPARE(weather().ticksLeft, QcWeatherCycle::lengthOf(QcServerConfig{}.seed, spell, QcWeatherKind::Thunder));

        chat(u"/weather"_s);
        QVERIFY(m_client->chatLines.at(m_client->chatLines.size() - 2).contains(u"thunderstorm"_s));

        const QcWeatherState before = weather();
        chat(u"/weather snow"_s);
        chat(u"/weather rain 0"_s);
        chat(u"/weather rain 1 2"_s);
        QCOMPARE(weather(), before);
        QVERIFY(m_client->chatLines.last().startsWith(u"Usage: /weather"_s));
    }

    void onlyOperatorsSetTheWeather()
    {
        join(notOperator());
        chat(u"/weather"_s);
        QCOMPARE(m_client->chatLines.last(), u"The weather changes in %1 s"_s.arg(qRound64(double(weather().ticksLeft) / 20.0)));
        chat(u"/weather rain"_s);
        QCOMPARE(weather().kind, QcWeatherKind::Clear);
        QVERIFY(m_client->chatLines.last().contains(u"Only operators"_s));
    }

    void weatherCycleOffHoldsTheWeather()
    {
        join();
        chat(u"/gamerule weatherCycle false"_s);
        chat(u"/weather rain 1"_s);
        QVERIFY(m_client->chatLines.last().contains(u"weatherCycle is false"_s));
        ticks(300);
        QCOMPARE(weather().kind, QcWeatherKind::Rain);
        QCOMPARE(weather().rain, 1.0f);

        // Turned on again, the second the spell had left runs out.
        chat(u"/gamerule weatherCycle true"_s);
        ticks(19);
        QCOMPARE(weather().kind, QcWeatherKind::Rain);
        ticks(1);
        QCOMPARE(weather().kind, QcWeatherKind::Clear);
    }

    void savedWithTheWorld()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        join(config);
        chat(u"/gamerule weatherCycle false"_s);
        chat(u"/weather thunder 100"_s);
        ticks(50);
        const QcWeatherState saved = weather();
        QVERIFY(saved.thunder > 0.2f && saved.thunder < 0.3f);
        QVERIFY(m_server->save().has_value());
        m_server->waitForStorage();
        m_client.reset();
        m_server.reset();

        join(config);
        QCOMPARE(weather().kind, saved.kind);
        QCOMPARE(weather().ticksLeft, saved.ticksLeft);
        QCOMPARE(weather().spell, saved.spell);
        // Two ticks of the join moved the strengths on, the cycle stayed off.
        QVERIFY(std::abs(weather().thunder - (saved.thunder + 2.0f / QcWeatherCycle::fadeTicks)) < 1e-5f);
        chat(u"/gamerule weatherCycle"_s);
        QCOMPARE(m_client->chatLines.last(), u"weatherCycle is false"_s);
    }

    void sentToEveryPlayerAlsoLate()
    {
        join();
        QVERIFY(m_client->weather);
        QCOMPARE(m_client->weather->rain, 0.0f);

        chat(u"/weather rain"_s);
        const int before = m_client->weatherMessages;
        fade();
        QCOMPARE(m_client->weather->rain, 1.0f);
        QCOMPARE(m_client->weather->thunder, 0.0f);
        // About one message per sendStep of the fade, not one per tick.
        const int sent = m_client->weatherMessages - before;
        QVERIFY2(sent >= 15 && sent <= 25, qPrintable(QString::number(sent)));

        const std::unique_ptr<RawClient> late = joinSecond(u"Late"_s);
        QVERIFY(late->weather);
        QCOMPARE(late->weather->rain, 1.0f);
        // Nothing changes, nothing is sent.
        const int settled = m_client->weatherMessages;
        ticks(40);
        QCOMPARE(m_client->weatherMessages, settled);
    }

    void climateByBiomeAndHeight()
    {
        const auto climate = [this](const char *biome) { return QcWeather::climateOf(m_content->biome(QString::fromLatin1(biome))); };
        QCOMPARE(QcWeather::precipitationAt(climate("qtcraft:tundra"), 64), QcPrecipitation::Snow);
        QCOMPARE(QcWeather::precipitationAt(climate("qtcraft:taiga"), 70), QcPrecipitation::Snow);
        QCOMPARE(QcWeather::precipitationAt(climate("qtcraft:desert"), 70), QcPrecipitation::None);
        QCOMPARE(QcWeather::precipitationAt(climate("qtcraft:plains"), 70), QcPrecipitation::Rain);
        QCOMPARE(QcWeather::precipitationAt(climate("qtcraft:plains"), QcWeather::snowHeight), QcPrecipitation::Snow);
        QCOMPARE(QcWeather::precipitationAt(climate("qtcraft:mountains"), 111), QcPrecipitation::Rain);
        QCOMPARE(QcWeather::precipitationAt(climate("qtcraft:mountains"), 112), QcPrecipitation::Snow);
        // Generators without biomes: rain up to snowHeight.
        QCOMPARE(QcWeather::climateOf(nullptr), (QcWeather::Climate{false, QcWeather::snowHeight}));

        // The server reads the climate from the generator's biome of the column.
        join({}, std::make_unique<QcTerrainGenerator>(m_content, 42));
        const QcTerrainGenerator generator(m_content, 42);
        for (int x = -2000; x <= 2000; x += 250) {
            const QcBiomeDefinition *biome = m_content->biome(generator.biomeAt(x, 300));
            QCOMPARE(m_server->systems().weather.climateAt(x, 300), QcWeather::climateOf(biome));
        }
    }

    void fallsOnlyUnderTheOpenSky()
    {
        join();
        const QcBlockPos head = m_feet.offset(0, 1, 0);
        QCOMPARE(m_server->systems().weather.precipitationAt(head), QcPrecipitation::None);

        chat(u"/weather rain"_s);
        ticks(QcWeatherCycle::fadeTicks / 10);
        // Below rainThreshold the sky darkens but nothing falls yet.
        QCOMPARE(m_server->systems().weather.precipitationAt(head), QcPrecipitation::None);
        fade();
        QVERIFY(m_server->systems().weather.isRainingAt(head));
        QVERIFY(m_server->systems().weather.openToSky(m_feet));

        // A roof three blocks up keeps it off everything below; above the roof it rains.
        QVERIFY(m_server->world().setBlock(m_feet.offset(0, 3, 0), id("qtcraft:stone")));
        QVERIFY(!m_server->systems().weather.isRainingAt(head));
        QVERIFY(!m_server->systems().weather.isRainingAt(m_feet));
        QVERIFY(m_server->systems().weather.isRainingAt(m_feet.offset(0, 4, 0)));
        // Glass keeps rain out too; a torch (no sky stop) does not.
        QVERIFY(m_server->world().setBlock(m_feet.offset(0, 3, 0), id("qtcraft:glass")));
        QVERIFY(!m_server->systems().weather.isRainingAt(head));
        QVERIFY(m_server->world().setBlock(m_feet.offset(0, 3, 0), id("qtcraft:torch")));
        QVERIFY(m_server->systems().weather.isRainingAt(head));
        // Columns that are not loaded are dry.
        QVERIFY(!m_server->systems().weather.openToSky({100000, 80, 100000}));
    }

    void snowAboveTheSnowHeight()
    {
        join();
        chat(u"/weather rain"_s);
        fade();
        const QcBlockPos high{m_feet.x, QcWeather::snowHeight + 5, m_feet.z};
        QVERIFY(m_server->systems().weather.isSnowingAt(high));
        QVERIFY(m_server->systems().weather.isRainingAt(m_feet.offset(0, 1, 0)));
        QCOMPARE(m_client->weather->climate.snowFrom, QcWeather::snowHeight);
    }

    // The climate cache drops its oldest columns when full; what it answers stays the same.
    void climateCacheKeepsAnsweringTheSame()
    {
        join();
        const QcWeather::Climate here = m_server->systems().weather.climateAt(m_feet.x, m_feet.z);
        for (int i = 0; i < 100'000; ++i) {
            m_server->systems().weather.climateAt(i % 400, i / 400);
            if (i % 10'000 == 0) {
                QCOMPARE(m_server->systems().weather.climateAt(m_feet.x, m_feet.z), here);
            }
        }
        QCOMPARE(m_server->systems().weather.climateAt(m_feet.x, m_feet.z), here);
    }

    void lightningOnlyInThunderAndIntoRain()
    {
        join();
        QList<QcBlockPos> struck;
        m_server->systems().weather.setStrikeHook([&struck](QcBlockPos pos) { struck.append(pos); });

        chat(u"/gamerule weatherCycle false"_s);
        chat(u"/weather rain"_s);
        ticks(1000);
        QCOMPARE(m_server->systems().weather.strikes(), 0);

        // Strikes fall only into the loaded columns around the player (about a fifth of the area in reach),
        // on average one in every 200 ticks of thunder: about a dozen in these ticks.
        chat(u"/weather thunder"_s);
        ticks(12000);
        QVERIFY2(m_server->systems().weather.strikes() >= 5, qPrintable(QString::number(m_server->systems().weather.strikes())));
        QCOMPARE(qsizetype(m_server->systems().weather.strikes()), struck.size());
        QCOMPARE(m_client->lightning.size(), struck.size());
        for (const QcBlockPos &pos : std::as_const(struck)) {
            QVERIFY(std::abs(pos.x - m_feet.x) <= QcServerWeather::strikeReach + 1);
            QVERIFY(std::abs(pos.z - m_feet.z) <= QcServerWeather::strikeReach + 1);
            QVERIFY(m_server->systems().weather.isRainingAt(pos));
            QVERIFY(!m_server->systems().weather.openToSky(pos.offset(0, -1, 0)));
        }

        // A strike called directly reaches the player within hearing and the hook.
        m_server->systems().weather.strike(m_feet.offset(30, 0, 0));
        QcTestUtil::pump();
        QCOMPARE(struck.last(), m_feet.offset(30, 0, 0));
        QCOMPARE(m_client->lightning.last().x, m_feet.x + 30.5);
    }

    // With several players each gets strikes near itself, and a strike is told only to the players
    // within hearing.
    void lightningWithSeveralPlayers()
    {
        join();
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        auto other = std::make_unique<RawClient>(std::move(clientEnd));
        other->hello(u"Other"_s);
        QcTestUtil::pump();
        ticks(3);

        // The raw player goes far beyond hearing (and stays in the air: the server moves raw players
        // only on input); the other stays at the spawn.
        const QcBlockPos away = m_feet.offset(int(QcServerWeather::strikeHearing) * 3, 0, 0);
        chat(u"/tp %1 %2 %3"_s.arg(away.x + 0.5).arg(QcWorld::worldHeight - 20).arg(away.z + 0.5));
        ticks(200);
        const QcVec3 here = m_client->motion.position;
        const QcVec3 there = other->motion.position;
        QVERIFY(std::abs(here.x - there.x) > QcServerWeather::strikeHearing);

        // A strike near one player reaches only that one.
        const QcBlockPos nearOther{int(std::floor(there.x)) + 10, int(std::floor(there.y)), int(std::floor(there.z))};
        m_server->systems().weather.strike(nearOther);
        QcTestUtil::pump();
        QCOMPARE(other->lightning.size(), 1);
        QCOMPARE(m_client->lightning.size(), 0);
        const QcBlockPos nearRaw{int(std::floor(here.x)) - 10, int(std::floor(here.y)), int(std::floor(here.z))};
        m_server->systems().weather.strike(nearRaw);
        QcTestUtil::pump();
        QCOMPARE(other->lightning.size(), 1);
        QCOMPARE(m_client->lightning.size(), 1);

        // In a thunderstorm both players get strikes of their own, each within its reach.
        QList<QcBlockPos> struck;
        m_server->systems().weather.setStrikeHook([&struck](QcBlockPos pos) { struck.append(pos); });
        chat(u"/gamerule weatherCycle false"_s);
        chat(u"/weather thunder"_s);
        ticks(6000);
        const auto near = [](QcBlockPos pos, const QcVec3 &player) {
            return std::abs(pos.x - player.x) <= QcServerWeather::strikeReach + 1 && std::abs(pos.z - player.z) <= QcServerWeather::strikeReach + 1;
        };
        const auto nearHere = std::ranges::count_if(struck, [&](QcBlockPos pos) { return near(pos, here); });
        const auto nearThere = std::ranges::count_if(struck, [&](QcBlockPos pos) { return near(pos, there); });
        QVERIFY2(nearHere >= 3 && nearThere >= 3, qPrintable(u"%1 %2"_s.arg(nearHere).arg(nearThere)));
        QCOMPARE(nearHere + nearThere, struck.size());
        QCOMPARE(m_client->lightning.size(), 1 + nearHere);
        QCOMPARE(other->lightning.size(), 1 + nearThere);
    }
};

QTEST_GUILESS_MAIN(TestWeather)
#include "test_weather.moc"
