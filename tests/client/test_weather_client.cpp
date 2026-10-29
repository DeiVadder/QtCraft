// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/light/QcSky.h"
#include "clientcore/session/QcClientWorld.h"
#include "clientcore/weather/QcClientWeather.h"
#include "clientcore/weather/QcDropPool.h"
#include "clientcore/weather/QcPrecipitationField.h"
#include "common/content/registry/QcSoundKeys.h"
#include "common/protocol/QcProtocol.h"
#include "common/world/QcDayCycle.h"
#include "QcTestWorld.h"

#include <QElapsedTimer>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

// The weather on the client (stage 35.1, decision E-395): the messages on the wire; the shown
// strengths ease towards the received ones; a strike flashes at once and thunders after the
// sound's travel time; rain and snow particles appear only above the top of their column, only in
// loaded columns, as snow where the climate says, and not at all in a dry climate; the sky darkens
// in rain and brightens in a flash.
class TestWeatherClient : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = std::make_shared<const QcContentRegistry>(qcMakeTestContent());

    QcBlockId stone() const { return *m_content->blockId(u"test:stone"_s); }

    // Chunks x -1..0, z -1..0 (blocks -16..15) with a stone floor at y 60 and a stone roof at y 70
    // over the chunk (0, 0).
    std::unique_ptr<QcClientWorld> roofedWorld() const
    {
        auto world = std::make_unique<QcClientWorld>(m_content);
        for (int cx = -1; cx <= 0; ++cx) {
            for (int cz = -1; cz <= 0; ++cz) {
                auto chunk = std::make_shared<QcChunk>(QcChunkPos{cx, cz});
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    for (int z = 0; z < QcWorld::chunkSize; ++z) {
                        chunk->setBlock(x, 60, z, stone());
                        if (cx == 0 && cz == 0) {
                            chunk->setBlock(x, 70, z, stone());
                        }
                    }
                }
                world->setChunk(chunk);
            }
        }
        return world;
    }

    static QcClientWeather raining(float rain, QcWeather::Climate climate = {})
    {
        QcClientWeather weather;
        weather.receive({rain, 0.0f, climate});
        for (int tick = 0; tick < 200; ++tick) {
            weather.tick(static_cast<quint64>(tick));
        }
        return weather;
    }

    // Runs the field for `frames` frames of 1/60 s around `eye`.
    static void run(QcPrecipitationField &field, const QcClientWorld &world, const QcClientWeather &weather, const QcVec3 &eye, int frames)
    {
        for (int frame = 0; frame < frames; ++frame) {
            field.update(world, weather, eye, 1.0 / 60.0);
        }
    }

private slots:
    void messagesSurviveTheWire()
    {
        const QcMsg::Weather weather{0.5f, 0.25f, {false, 112}};
        const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(weather), QcProtocol::Peer::Server);
        QVERIFY(decoded.has_value());
        QCOMPARE(std::get<QcMsg::Weather>(*decoded), weather);

        const QcMsg::Lightning strike{{10.5, 70.0, -3.5}};
        const QcResult<QcMessage> struck = QcProtocol::decode(QcProtocol::encode(strike), QcProtocol::Peer::Server);
        QVERIFY(struck.has_value());
        QCOMPARE(std::get<QcMsg::Lightning>(*struck).position, strike.position);

        // Strengths outside 0..1 and messages from a client are refused.
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::Weather{1.5f, 0.0f, {}}), QcProtocol::Peer::Server).has_value());
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::Weather{std::nanf(""), 0.0f, {}}), QcProtocol::Peer::Server).has_value());
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(weather), QcProtocol::Peer::Client).has_value());
    }

    void strengthsEaseTowardsTheReceivedOnes()
    {
        QcClientWeather weather;
        weather.receive({1.0f, 0.5f, {}});
        weather.tick(1);
        QCOMPARE(weather.rain(), QcClientWeather::easePerTick);
        for (quint64 tick = 2; tick <= 100; ++tick) {
            weather.tick(tick);
        }
        QCOMPARE(weather.rain(), 1.0f);
        QCOMPARE(weather.thunder(), 0.5f);
        QCOMPARE(weather.rainAt(70), 1.0f);
        // Snow and dry climates have no rain to hear.
        weather.receive({1.0f, 0.5f, {false, 100}});
        QCOMPARE(weather.precipitationAt(120), QcPrecipitation::Snow);
        QCOMPARE(weather.rainAt(120), 0.0f);
        weather.receive({1.0f, 0.5f, {true, QcWeather::noSnow}});
        QCOMPARE(weather.precipitationAt(70), QcPrecipitation::None);
    }

    void thunderFollowsTheFlashBySoundSpeed()
    {
        QcClientWeather weather;
        const QcVec3 listener{0.0, 70.0, 0.0};
        QCOMPARE(weather.flash(0.0), 0.0f);

        // 171.5 m away: heard half a second (10 ticks) after the flash, placed nearer and quieter.
        weather.lightning({{343.0 / 2.0, 70.0, 0.0}}, listener, 100);
        QCOMPARE(weather.flash(100.5), 1.0f);
        QVERIFY(weather.flash(102.5) < 0.5f);
        QVERIFY(weather.flash(103.5) > 0.5f);
        QCOMPARE(weather.flash(100.0 + QcClientWeather::flashTicks), 0.0f);
        QVERIFY(weather.tick(109).isEmpty());
        const QList<QcSoundCue> due = weather.tick(110);
        QCOMPARE(due.size(), 1);
        QCOMPARE(due[0].key, QString::fromLatin1(QcSoundKeys::weatherThunder));
        QVERIFY(std::abs(due[0].position.x - QcClientWeather::thunderPlacement) < 1e-9);
        QVERIFY(std::abs(due[0].volume - float(QcClientWeather::thunderPlacement / 171.5)) < 1e-4f);

        // Close by: at the strike, full volume, a tick later (10 m: 0.03 s); beyond hearing: only
        // the flash.
        weather.lightning({{10.0, 70.0, 0.0}}, listener, 200);
        QVERIFY(weather.tick(200).isEmpty());
        const QList<QcSoundCue> near = weather.tick(201);
        QCOMPARE(near.size(), 1);
        QCOMPARE(near[0].volume, 1.0f);
        QCOMPARE(near[0].position.x, 10.0);
        weather.lightning({{1000.0, 70.0, 0.0}}, listener, 300);
        for (quint64 tick = 300; tick < 400; ++tick) {
            QVERIFY(weather.tick(tick).isEmpty());
        }
        QCOMPARE(weather.flash(300.5), 1.0f);
        QCOMPARE(weather.strikes(), 3);
    }

    void particlesOnlyUnderTheOpenSky()
    {
        const std::unique_ptr<QcClientWorld> world = roofedWorld();
        const QcClientWeather weather = raining(1.0f);
        QcPrecipitationField field(600);
        const QcVec3 eye{0.5, 65.0, 0.5};
        int checked = 0;
        for (int round = 0; round < 20; ++round) {
            run(field, *world, weather, eye, 10);
            QVERIFY(field.particles().size() <= 600);
            for (const QcPrecipitationField::Particle &particle : field.particles()) {
                const QcVec3 &p = particle.position;
                QVERIFY2(!particle.snow, "rain below the snow height");
                // Only loaded columns, above their top: the roof (70) or the floor (60).
                QVERIFY2(p.x >= -16.0 && p.x < 16.0 && p.z >= -16.0 && p.z < 16.0, qPrintable(u"%1 %2"_s.arg(p.x).arg(p.z)));
                const bool roofed = p.x >= 0.0 && p.z >= 0.0;
                QVERIFY2(p.y >= (roofed ? 71.0 : 61.0), qPrintable(u"%1 %2 %3"_s.arg(p.x).arg(p.y).arg(p.z)));
                ++checked;
            }
        }
        QVERIFY(checked > 5000);

        // One drop per particle at its position; the streak reaches up from it, never below.
        QcDropPool pool;
        std::shared_ptr<const QList<QcPrecipitationDrop>> drops = pool.fill(field, 1.0f, QcVec3{});
        QCOMPARE(drops->size(), field.particles().size());
        for (qsizetype i = 0; i < field.particles().size(); ++i) {
            QCOMPARE((*drops)[i].y, float(field.particles()[i].position.y));
            QCOMPARE((*drops)[i].snow, quint8(0));
        }
        QVERIFY(QcPrecipitationField::rainStreak().y > 0.0);

        // A list nobody holds any more keeps its memory: filling it again allocates nothing. A list
        // still held is left alone.
        const QcPrecipitationDrop *memory = drops->constData();
        drops.reset();
        drops = pool.fill(field, 0.5f, QcVec3{});
        QCOMPARE(drops->constData(), memory);
        QVERIFY(drops->constFirst().alpha < 190);
        const std::shared_ptr<const QList<QcPrecipitationDrop>> next = pool.fill(field, 0.5f, QcVec3{});
        QVERIFY(next->constData() != memory);
        QCOMPARE(field.columnTop(*world, 3, 3), 70);
        QCOMPARE(field.columnTop(*world, -3, 3), 60);
        QCOMPARE(field.columnTop(*world, 40, 3), QcWorld::worldHeight);
    }

    void particlesFollowStrengthAndClimate()
    {
        const std::unique_ptr<QcClientWorld> world = roofedWorld();
        const QcVec3 eye{-8.0, 62.0, -8.0};

        QcPrecipitationField none(600);
        run(none, *world, raining(0.0f), eye, 60);
        QVERIFY(none.particles().isEmpty());

        QcPrecipitationField half(600);
        run(half, *world, raining(0.5f), eye, 120);
        QVERIFY(half.particles().size() > 100 && half.particles().size() <= 300);

        QcPrecipitationField snow(600);
        run(snow, *world, raining(1.0f, {false, 0}), eye, 120);
        QVERIFY(!snow.particles().isEmpty());
        QVERIFY(std::ranges::all_of(snow.particles(), &QcPrecipitationField::Particle::snow));
        // One drop per flake; flakes fade in with the strength like rain.
        QcDropPool pool;
        const std::shared_ptr<const QList<QcPrecipitationDrop>> flakes = pool.fill(snow, 1.0f, QcVec3{});
        QCOMPARE(flakes->size(), snow.particles().size());
        QVERIFY(std::ranges::all_of(*flakes, [](QcPrecipitationDrop drop) { return drop.snow != 0; }));
        const std::shared_ptr<const QList<QcPrecipitationDrop>> faint = pool.fill(snow, 0.1f, QcVec3{});
        QVERIFY(faint->constFirst().alpha < flakes->constFirst().alpha);

        // A shower that stops in a dry climate: what is falling lands, nothing new starts.
        QcPrecipitationField dry(600);
        run(dry, *world, raining(1.0f), eye, 60);
        QVERIFY(!dry.particles().isEmpty());
        run(dry, *world, raining(1.0f, {true, QcWeather::noSnow}), eye, 240);
        QVERIFY(dry.particles().isEmpty());
    }

    // Measurement (QTCRAFT_WEATHER_MEASURE=1): CPU time per frame of a full field (default budget)
    // under the open sky, update and filling the drops, as the render bridge runs them, and the
    // bytes uploaded per frame.
    void measureField()
    {
        if (qEnvironmentVariableIntValue("QTCRAFT_WEATHER_MEASURE") != 1) {
            QSKIP("measurement: QTCRAFT_WEATHER_MEASURE=1");
        }
        const std::unique_ptr<QcClientWorld> world = roofedWorld();
        for (const QcWeather::Climate climate : {QcWeather::Climate{}, QcWeather::Climate{false, 0}}) {
            const QcClientWeather weather = raining(1.0f, climate);
            QcPrecipitationField field;
            const QcVec3 eye{-8.0, 62.0, -8.0};
            run(field, *world, weather, eye, 300);
            constexpr int frames = 3000;
            QElapsedTimer clock;
            clock.start();
            QcDropPool pool;
            qsizetype bytes = 0;
            for (int frame = 0; frame < frames; ++frame) {
                field.update(*world, weather, eye, 1.0 / 60.0);
                bytes += pool.fill(field, 1.0f, QcVec3{})->size() * static_cast<qsizetype>(sizeof(QcPrecipitationDrop));
            }
            qInfo("%s: %lld particles, %.4f ms per frame, %lld bytes uploaded per frame", climate.snowFrom == 0 ? "snow" : "rain",
                  static_cast<qint64>(field.particles().size()), static_cast<double>(clock.nsecsElapsed()) / 1e6 / frames, static_cast<qint64>(bytes / frames));
        }
    }

    void skyDarkensInRainAndBrightensInAFlash()
    {
        const QcSkyState noon = QcSky::at(150.0);
        const QcSkyState clear = QcSky::withWeather(noon, 0.0, 0.0, 0.0);
        QCOMPARE(clear.zenith, noon.zenith);
        QCOMPARE(clear.daylight, noon.daylight);
        QCOMPARE(clear.overcast, 0.0);
        QCOMPARE(clear.fogScale, 1.0);

        const QcSkyState rain = QcSky::withWeather(noon, 1.0, 0.0, 0.0);
        const QcSkyState storm = QcSky::withWeather(noon, 1.0, 1.0, 0.0);
        QVERIFY(rain.zenith.length() < noon.zenith.length());
        QVERIFY(storm.zenith.length() < rain.zenith.length());
        QVERIFY(rain.daylight < noon.daylight && storm.daylight < rain.daylight);
        QVERIFY(storm.daylight >= QcDayCycle::minimumDaylight);
        QCOMPARE(rain.overcast, 1.0);
        QVERIFY(rain.fogScale < 1.0 && storm.fogScale < rain.fogScale);

        const QcSkyState flash = QcSky::withWeather(QcSky::at(450.0), 1.0, 1.0, 1.0);
        QVERIFY(flash.daylight > 0.7);
        QVERIFY(flash.zenith.length() > QcSky::withWeather(QcSky::at(450.0), 1.0, 1.0, 0.0).zenith.length());
    }
};

QTEST_GUILESS_MAIN(TestWeatherClient)
#include "test_weather_client.moc"
