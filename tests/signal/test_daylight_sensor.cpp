// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcDayCycle.h"
#include "fixtures/QcPlayerUseFixture.h"
#include "server/chunks/QcBlockIndex.h"
#include "server/signal/QcDaylightSensors.h"
#include "server/signal/QcServerCircuits.h"

#include <numbers>

using namespace Qt::StringLiterals;

// The daylight sensor (decision E-982): every 20 ticks a loaded sensor takes the strength the sky
// above it and the sun give, reversed when it is inverted; dust and components around it get the
// signal, no block does.
class TestDaylightSensor : public QcPlayerUseFixture
{
    Q_OBJECT

private:
    static constexpr int interval = QcSignalDefinition::daylightIntervalTicks;
    static constexpr double noon = QcDayCycle::dayLength * 0.25;
    static constexpr double midnight = QcDayCycle::dayLength * 0.75;

    int power(int x, int y, int z) const { return property(x, y, z, u"power"_s).toInt(); }
    const QcBlockIndex &index() const { return m_server->world().daylightSensors(); }

    // Stops the clock of the day at `worldTime`, with the sky clear and steady.
    void setSky(double worldTime, const char *weather = "clear")
    {
        chat(u"/gamerule daylightCycle false"_s);
        chat(u"/gamerule weatherCycle false"_s);
        chat(u"/weather %1"_s.arg(QString::fromLatin1(weather)));
        m_server->setWorldTime(worldTime);
    }

    // Stone above and on all four sides of the cell at (`x`, 0, `z`), so that no sky light comes in.
    void closeIn(int x, int z)
    {
        put(x, 1, z, u"stone"_s);
        for (const QcBlockFace face : {QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West}) {
            putAt(qcNeighbour(at(x, 0, z), face), id("qtcraft:stone"));
        }
    }

    // Runs ticks until a sensor pass has just happened.
    void toNextPass()
    {
        ticks(1);
        while (m_server->tickCount() % interval != 0) {
            ticks(1);
        }
    }

    // Lets the strengths of rain and thunder settle at their spell's kind.
    void settleWeather(float rain, float thunder)
    {
        const QcWeatherState &weather = m_server->systems().weather.state();
        for (int tick = 0; tick < 2000 && (weather.rain != rain || weather.thunder != thunder); ++tick) {
            ticks(1);
        }
        QCOMPARE(weather.rain, rain);
        QCOMPARE(weather.thunder, thunder);
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
        m_unloadChunks = false;
    }

    void craftedFromGlassQuartzAndAnyWoodenSlabs()
    {
        buildArea();
        const auto cells = [](const QString &slab) {
            return QList<std::pair<int, QString>>{{0, u"glass"_s}, {1, u"glass"_s}, {2, u"glass"_s}, {3, u"quartz"_s}, {4, u"quartz"_s},
                                                  {5, u"quartz"_s}, {6, slab}, {7, slab}, {8, slab}};
        };
        QCOMPARE(crafted(cells(u"oak_slab"_s), 3), (std::pair{u"qtcraft:daylight_sensor"_s, 1}));
        QCOMPARE(crafted(cells(u"birch_slab"_s), 3), (std::pair{u"qtcraft:daylight_sensor"_s, 1}));
        // Stone slabs are no wooden slabs.
        QCOMPARE(crafted(cells(u"cobblestone_slab"_s), 3), (std::pair<QString, int>{}));
    }

    // The role model's numbers: the light less the darkening, by the eased height of the sun.
    void strengthFollowsTheFormula()
    {
        constexpr double pi = std::numbers::pi;
        QCOMPARE(QcDaylightSensors::strength(15, 0, 0.0, false), 15);
        QCOMPARE(QcDaylightSensors::strength(15, 3, 0.0, false), 12);
        // Sunset and sunrise: 7 levels left, a fifth back towards noon gives cos(0.4 pi).
        QCOMPARE(QcDaylightSensors::strength(15, 8, pi / 2.0, false), 2);
        QCOMPARE(QcDaylightSensors::strength(15, 8, 3.0 * pi / 2.0, false), 2);
        QCOMPARE(QcDaylightSensors::strength(15, 11, pi, false), 0);
        QCOMPARE(QcDaylightSensors::strength(0, 0, 0.0, false), 0);
        // Inverted ignores the sun, and a dark sky cannot lift it past 15.
        QCOMPARE(QcDaylightSensors::strength(15, 0, 0.0, true), 0);
        QCOMPARE(QcDaylightSensors::strength(15, 11, pi, true), 11);
        QCOMPARE(QcDaylightSensors::strength(0, 11, pi, true), 15);
    }

    // Fixed values between noon and night, by the sun of this game's day curve.
    void theSkyDarkensBetweenTheFixedPoints()
    {
        const auto darkening = [](double phase, float rain, float thunder) {
            return QcDayCycle::skyDarkening(phase * QcDayCycle::dayLength, rain, thunder);
        };
        QCOMPARE(darkening(0.45, 0.0f, 0.0f), 3);
        QCOMPARE(darkening(0.5, 0.0f, 0.0f), 8);
        QCOMPARE(darkening(0.55, 0.0f, 0.0f), 11);
        QCOMPARE(darkening(0.5, 1.0f, 0.0f), 9);
        QCOMPARE(darkening(0.25, 1.0f, 1.0f), 5);
    }

    void noonGivesFifteenToDustAndComponentsButNoBlock()
    {
        buildArea();
        setSky(noon);
        put(5, 0, 5, u"daylight_sensor"_s);
        put(6, 0, 5, u"signal_dust"_s);
        put(7, 0, 5, u"signal_lamp"_s);
        put(5, 0, 6, u"signal_lamp"_s);
        put(4, 0, 5, u"stone"_s);
        put(3, 0, 5, u"signal_lamp"_s);
        // Twin without a sensor.
        put(6, 0, 9, u"signal_dust"_s);
        put(7, 0, 9, u"signal_lamp"_s);
        toNextPass();
        ticks(2);

        QCOMPARE(power(5, 0, 5), 15);
        QCOMPARE(power(6, 0, 5), 15);
        QVERIFY(isBlock(7, 0, 5, "signal_lamp_lit"));
        QVERIFY(isBlock(5, 0, 6, "signal_lamp_lit"));
        QVERIFY(isBlock(7, 0, 9, "signal_lamp"));
        // The stone next to it gets no power to pass on.
        QVERIFY(isBlock(3, 0, 5, "signal_lamp"));

        // Broken, it takes the signal along.
        put(5, 0, 5, u"air"_s);
        ticks(10);
        QCOMPARE(power(6, 0, 5), 0);
        QVERIFY(isBlock(7, 0, 5, "signal_lamp"));
        QVERIFY(isBlock(5, 0, 6, "signal_lamp"));
    }

    void nightGivesNothingAndTheMorningLittle()
    {
        buildArea();
        setSky(noon);
        put(5, 0, 5, u"daylight_sensor"_s);
        toNextPass();
        QCOMPARE(power(5, 0, 5), 15);

        m_server->setWorldTime(midnight);
        toNextPass();
        QCOMPARE(power(5, 0, 5), 0);

        // Early in the day it is under way but not there yet.
        m_server->setWorldTime(QcDayCycle::dayLength * 0.1);
        toNextPass();
        QVERIFY2(power(5, 0, 5) > 0 && power(5, 0, 5) < 15, qPrintable(QString::number(power(5, 0, 5))));
    }

    void rainAndThunderDimTheNoon()
    {
        buildArea();
        setSky(noon, "rain");
        put(5, 0, 5, u"daylight_sensor"_s);
        settleWeather(1.0f, 0.0f);
        toNextPass();
        QCOMPARE(power(5, 0, 5), 12);

        chat(u"/weather thunder"_s);
        settleWeather(1.0f, 1.0f);
        toNextPass();
        QCOMPARE(power(5, 0, 5), 10);
    }

    void aRoofShadesItAndGlassDoesNot()
    {
        buildArea();
        setSky(noon);
        put(5, 0, 5, u"daylight_sensor"_s);
        closeIn(5, 5);
        put(9, 0, 5, u"daylight_sensor"_s);
        put(9, 1, 5, u"glass"_s);
        // A single block overhead: light comes in from the sides, one level less.
        put(13, 0, 5, u"daylight_sensor"_s);
        put(13, 1, 5, u"stone"_s);
        toNextPass();
        QCOMPARE(power(5, 0, 5), 0);
        QCOMPARE(power(9, 0, 5), 15);
        QCOMPARE(power(13, 0, 5), 14);

        put(5, 1, 5, u"air"_s);
        toNextPass();
        QCOMPARE(power(5, 0, 5), 15);
    }

    void usingItTurnsItRoundAtOnce()
    {
        buildArea();
        setSky(noon);
        put(5, 0, 5, u"daylight_sensor"_s);
        put(6, 0, 5, u"signal_dust"_s);
        toNextPass();
        QCOMPARE(power(5, 0, 5), 15);

        flip(5, 0, 5);
        QVERIFY(flag(5, 0, 5, u"inverted"_s));
        QCOMPARE(power(5, 0, 5), 0);
        ticks(1);
        QCOMPARE(power(6, 0, 5), 0);

        m_server->setWorldTime(midnight);
        toNextPass();
        QCOMPARE(power(5, 0, 5), 11);
        ticks(1);
        QCOMPARE(power(6, 0, 5), 11);

        // Under a roof an inverted sensor gives all it has, by day as well.
        m_server->setWorldTime(noon);
        closeIn(5, 5);
        toNextPass();
        QCOMPARE(power(5, 0, 5), 15);

        flip(5, 0, 5);
        QVERIFY(!flag(5, 0, 5, u"inverted"_s));
        QCOMPARE(power(5, 0, 5), 0);
    }

    void aPlayerUsesItInsteadOfPlacingAgainstIt()
    {
        join();
        setSky(noon);
        const QcBlockPos sensor = m_feet.offset(2, 0, 0);
        putAt(sensor, id("qtcraft:daylight_sensor"));
        toNextPass();
        hold(u"stone"_s, 4);

        send(QcMsg::UseItem{sensor, QcBlockFace::West});
        ticks(1);
        QCOMPARE(propertyAt(sensor, u"inverted"_s), u"true"_s);
        QCOMPARE(*m_server->world().blockAt(sensor.offset(-1, 0, 0)), qcAirBlockId);
    }

    void readsTheSkyOnlyEveryTwentyTicks()
    {
        buildArea();
        setSky(noon);
        put(5, 0, 5, u"daylight_sensor"_s);
        toNextPass();
        QCOMPARE(power(5, 0, 5), 15);

        m_server->setWorldTime(midnight);
        for (int tick = 1; tick < interval; ++tick) {
            ticks(1);
            QCOMPARE(power(5, 0, 5), 15);
        }
        ticks(1);
        QCOMPARE(m_server->tickCount() % interval, quint64{0});
        QCOMPARE(power(5, 0, 5), 0);
    }

    void keepsItsStateThroughSaving()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        buildArea(config);
        setSky(midnight);
        put(5, 0, 5, u"daylight_sensor[inverted=true]"_s);
        put(6, 0, 5, u"signal_dust"_s);
        put(7, 0, 5, u"signal_lamp"_s);
        toNextPass();
        ticks(2);
        QCOMPARE(power(5, 0, 5), 11);
        QCOMPARE(power(6, 0, 5), 11);

        QVERIFY(m_server->save().has_value());
        const QcBlockPos origin = m_origin;
        const qsizetype sensors = index().size();
        cleanup();
        join(config);
        m_origin = origin;

        // Back as it was, and still read: the index found it in the loaded chunk.
        QVERIFY(flag(5, 0, 5, u"inverted"_s));
        QCOMPARE(power(5, 0, 5), 11);
        QCOMPARE(index().size(), sensors);

        // The neighbours see no burst: no tick around the first pass drops or lifts the signal.
        for (int tick = 0; tick <= interval; ++tick) {
            QCOMPARE(power(6, 0, 5), 11);
            QVERIFY(isBlock(7, 0, 5, "signal_lamp_lit"));
            ticks(1);
        }

        m_server->setWorldTime(noon);
        toNextPass();
        QCOMPARE(power(5, 0, 5), 0);
    }

    void theIndexFollowsBlocks()
    {
        buildArea();
        setSky(noon);
        QCOMPARE(index().size(), 0);

        put(5, 0, 5, u"daylight_sensor"_s);
        put(9, 0, 5, u"daylight_sensor[inverted=true]"_s);
        QCOMPARE(index().size(), 2);
        QVERIFY(index().contains(at(5, 0, 5)));

        // A change of state keeps it, breaking it takes it out.
        toNextPass();
        QCOMPARE(power(5, 0, 5), 15);
        QCOMPARE(index().size(), 2);
        put(5, 0, 5, u"air"_s);
        QCOMPARE(index().size(), 1);
        QVERIFY(!index().contains(at(5, 0, 5)));
    }

    void anIndexThatIgnoresTouchingNotesNothing()
    {
        buildArea();
        const QcBlockId sensor = id("qtcraft:daylight_sensor");
        const QcBlockPos pos = at(5, 0, 5);
        std::vector<bool> marked(static_cast<std::size_t>(m_content->stateCount()));
        marked[static_cast<std::size_t>(sensor)] = true;
        QcBlockIndex noted(marked);
        QcBlockIndex ignored(marked, QcBlockIndex::Touching::Ignored);

        putAt(pos, sensor);
        const std::shared_ptr<const QcChunk> chunk = m_server->world().chunk(pos.chunk());
        for (QcBlockIndex *index : {&noted, &ignored}) {
            index->noteChunk(pos.chunk(), *chunk);
            index->noteBlock(pos, sensor);
            index->noteBlock(pos.offset(1, 0, 0), qcAirBlockId);
            QVERIFY(index->contains(pos));
        }
        QVERIFY(!noted.takeTouched().isEmpty());
        QVERIFY(ignored.takeTouched().isEmpty());
    }

    // A sensor at the edge of the loaded area changes no state while its neighbour is away: the
    // change would wait there as a signal and load the chunk (decision E-376).
    void atTheEdgeItAsksForNoTicketAndReadsOnceTheNeighbourIsBack()
    {
        m_unloadChunks = true;
        join();
        chat(u"/gamerule mobSpawning false"_s);
        setSky(midnight);
        const QcChunkPos home = m_feet.chunk();
        const QcChunkPos beyond{home.x + 2, home.z};
        const QcBlockPos sensor{beyond.x * QcWorld::chunkSize - 1, m_feet.y + 20, home.z * QcWorld::chunkSize + QcWorld::chunkSize / 2};
        ticks(interval);
        QVERIFY(m_server->world().chunk({home.x + 1, home.z}));
        QVERIFY(!m_server->world().chunk(beyond));
        QVERIFY(m_server->world().setBlock(sensor, id("qtcraft:daylight_sensor")));

        const quint64 given = m_server->chunkLoading().tickets().given();
        const quint64 refused = m_server->chunkLoading().tickets().refused();
        m_server->setWorldTime(noon);
        ticks(5 * interval);
        QCOMPARE(m_server->chunkLoading().tickets().given(), given);
        QCOMPARE(m_server->chunkLoading().tickets().refused(), refused);
        QVERIFY(!m_server->world().chunk(beyond));
        QCOMPARE(propertyAt(sensor, u"power"_s), u"0"_s);

        chat(u"/tp %1 %2 %3"_s.arg(m_feet.x + QcWorld::chunkSize).arg(m_feet.y).arg(m_feet.z));
        for (int tick = 0; tick < 400 && !m_server->world().chunk(beyond); ++tick) {
            ticks(1);
        }
        QVERIFY(m_server->world().chunk(beyond));
        toNextPass();
        QCOMPARE(propertyAt(sensor, u"power"_s), u"15"_s);
    }

    void chunksTakeTheirSensorsAlong()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        m_unloadChunks = true;
        buildArea(config);
        setSky(noon);
        put(5, 0, 5, u"daylight_sensor"_s);
        toNextPass();
        QCOMPARE(power(5, 0, 5), 15);
        const QcBlockPos sensor = at(5, 0, 5);
        const QcBlockPos home = m_feet;

        // Away: the chunk leaves, the index forgets the sensor and nothing reads it any more.
        chat(u"/tp %1 %2 %3"_s.arg(home.x + 30 * QcWorld::chunkSize).arg(home.y).arg(home.z));
        for (int tick = 0; tick < 400 && m_server->world().blockAt(sensor); ++tick) {
            ticks(1);
        }
        QVERIFY(!m_server->world().blockAt(sensor));
        QCOMPARE(index().size(), 0);
        m_server->setWorldTime(midnight);
        toNextPass();

        // Back: it comes as it was saved and takes the night at the next pass.
        chat(u"/tp %1 %2 %3"_s.arg(home.x).arg(home.y).arg(home.z));
        for (int tick = 0; tick < 400 && !m_server->world().blockAt(sensor); ++tick) {
            ticks(1);
        }
        QVERIFY(m_server->world().blockAt(sensor).has_value());
        QVERIFY(index().contains(sensor));
        toNextPass();
        QCOMPARE(power(5, 0, 5), 0);
    }
};

QTEST_GUILESS_MAIN(TestDaylightSensor)
#include "test_daylight_sensor.moc"
