// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/light/QcShadowCascades.h"
#include "clientcore/light/QcSky.h"
#include "clientcore/player/QcDials.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/worldgen/QcDimensionParser.h"
#include "common/protocol/QcProtocol.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worlds/QcLocalServerHost.h"

#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

Q_DECLARE_METATYPE(QcDimensionDefinition)

namespace {

constexpr auto emptyName = "test:empty";
constexpr auto netherName = "test:nether";
constexpr auto overworld = QcDimensionDefinition::overworldName;

QcDimensionDefinition parsed(const QJsonObject &json)
{
    QcResult<QcDimensionDefinition> dimension = QcDimensionParser::parseDimension(json, u"test"_s);
    if (!dimension) {
        qFatal("test dimension does not parse: %s", qPrintable(dimension.error()));
    }
    return std::move(*dimension);
}

// A dimension like the nether without its generator: fog, no sky light, no day, no weather.
QcDimensionDefinition netherLike()
{
    return parsed({{u"name"_s, QString::fromLatin1(netherName)},
                   {u"generator"_s, QJsonObject{{u"type"_s, u"void"_s}, {u"options"_s, u"platform=qtcraft:stone,radius=1,y=1"_s}}},
                   {u"ceiling"_s, 127},
                   {u"coordinateScale"_s, 8.0},
                   {u"sky"_s, u"nether"_s},
                   {u"fogColour"_s, u"#330808"_s},
                   {u"ambientLight"_s, 6},
                   {u"skyLight"_s, false},
                   {u"dayCycle"_s, false},
                   {u"weather"_s, false},
                   {u"compassesSpin"_s, true}});
}

// The base content with an empty test dimension without weather (decision E-978) and the
// nether-like one.
std::shared_ptr<QcContentRegistry> contentWithDimensions()
{
    std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
    content->worldgen().registerDimension(
        parsed({{u"name"_s, QString::fromLatin1(emptyName)},
                {u"generator"_s, QJsonObject{{u"type"_s, u"void"_s}, {u"options"_s, u"platform=qtcraft:stone,radius=1,y=1"_s}}},
                {u"weather"_s, false}}));
    content->worldgen().registerDimension(netherLike());
    return content;
}

QcServerConfig configIn(const QString &directory)
{
    QcServerConfig config;
    config.worldName = u"Client"_s;
    config.seed = 4711;
    config.viewDistance = 2;
    config.worldDirectory = directory;
    config.operators = {u"Traveller"_s, u"Stayer"_s};
    return config;
}

QcResult<QcMessage> roundTrip(const QcMsg::DimensionChange &message)
{
    return QcProtocol::decode(QcProtocol::encode(message), QcProtocol::Peer::Server);
}

// Whether `session` knows the entity `id`.
bool sees(const QcClientSession &session, quint32 id)
{
    return session.entities().contains(id);
}

} // namespace

// The client side of dimensions (decisions E-1002 to E-1004): the message, the world forgotten on a
// move, the sky, the dials and players in different dimensions.
class TestDimensionClient : public QObject
{
    Q_OBJECT

private slots:
    void theMessageCarriesTheClientsPartOfTheDimension()
    {
        const QcDimensionDefinition sent = netherLike();
        const QcResult<QcMessage> decoded = roundTrip({sent});
        QVERIFY2(decoded, qPrintable(decoded ? QString() : decoded.error()));
        const auto *change = std::get_if<QcMsg::DimensionChange>(&*decoded);
        QVERIFY(change);
        const QcDimensionDefinition &got = change->dimension;
        QCOMPARE(got.name, sent.name);
        QCOMPARE(got.translationKey, u"dimension.test.nether"_s);
        QCOMPARE(got.ceiling, 127);
        QCOMPARE(got.coordinateScale, 8.0);
        QCOMPARE(got.sky, QcDimensionDefinition::Sky::Nether);
        QCOMPARE(got.fogColour, std::optional<quint32>(0x330808));
        QCOMPARE(got.ambientLight, 6);
        QVERIFY(!got.skyLight && !got.dayCycle && !got.weather && got.compassesSpin);

        // The overworld without fog colour comes back as it went.
        const QcResult<QcMessage> plain = roundTrip({QcDimensionDefinition::defaultOverworld()});
        QVERIFY(plain);
        const QcDimensionDefinition &ow = std::get<QcMsg::DimensionChange>(*plain).dimension;
        QVERIFY(ow.isOverworld() && !ow.fogColour && ow.skyLight && ow.dayCycle && ow.weather && !ow.compassesSpin);
        QCOMPARE(ow.sky, QcDimensionDefinition::Sky::Overworld);
    }

    void invalidDimensionsAreRefused_data()
    {
        QTest::addColumn<QcDimensionDefinition>("dimension");
        QcDimensionDefinition base = netherLike();
        QcDimensionDefinition unnamed = base;
        unnamed.name.clear();
        QTest::newRow("no name") << unnamed;
        QcDimensionDefinition longName = base;
        longName.name = QString(QcDimensionDefinition::maxNameLength + 1, u'n');
        QTest::newRow("name too long") << longName;
        QcDimensionDefinition dark = base;
        dark.ambientLight = 16;
        QTest::newRow("ambient light above 15") << dark;
        QcDimensionDefinition scaled = base;
        scaled.coordinateScale = std::numeric_limits<double>::quiet_NaN();
        QTest::newRow("scale NaN") << scaled;
        QcDimensionDefinition fogged = base;
        fogged.fogColour = 0x1000000;
        QTest::newRow("fog beyond RGB") << fogged;
        QcDimensionDefinition sky = base;
        sky.sky = static_cast<QcDimensionDefinition::Sky>(3);
        QTest::newRow("unknown sky") << sky;
    }

    void invalidDimensionsAreRefused()
    {
        QFETCH(QcDimensionDefinition, dimension);
        QVERIFY(!roundTrip({dimension}));
    }

    void theSkyFollowsTheDimension()
    {
        // The overworld's sky is the plain sky of the time of day.
        const QcDimensionDefinition overworldSky = QcDimensionDefinition::defaultOverworld();
        const double night = QcDayCycle::dayLength * 0.75;
        const QcSkyState plain = QcSky::forDimension(overworldSky, night);
        QCOMPARE(plain.zenith, QcSky::at(night).zenith);
        QCOMPARE(plain.daylight, QcSky::at(night).daylight);
        QVERIFY(plain.celestial && plain.skyLight);
        QCOMPARE(plain.flashLight, 0.0);

        // Without day cycle the overworld's sky stands at noon whatever the time.
        QcDimensionDefinition timeless = overworldSky;
        timeless.dayCycle = false;
        QCOMPARE(QcSky::forDimension(timeless, night).sunDirection, QcSky::at(QcSky::fixedNoon).sunDirection);
        QVERIFY(QcSky::forDimension(timeless, night).sunDirection.y() > 0.9f);

        // The nether: its fog everywhere, no sun, no sky light, the ambient light as floor, close fog.
        const QcSkyState nether = QcSky::forDimension(netherLike(), night);
        const QVector3D fog(0x33 / 255.0f, 0x08 / 255.0f, 0x08 / 255.0f);
        QCOMPARE(nether.fogColor(), fog);
        QCOMPARE(nether.zenith, fog);
        QVERIFY(!nether.celestial && !nether.skyLight);
        QCOMPARE(nether.daylight, 0.0);
        QCOMPARE(nether.stars, 0.0);
        QVERIFY(nether.flashLight > 0.2 && nether.flashLight < 0.3);
        QVERIFY(nether.fogScale < 1.0);
        // Its default fog is red when the dimension names none.
        QcDimensionDefinition unfogged = netherLike();
        unfogged.fogColour.reset();
        const QVector3D red = QcSky::forDimension(unfogged, night).fogColor();
        QVERIFY(red.x() > 2.0f * red.y() && red.x() > 2.0f * red.z());

        // The end: a dark vault, darker above than at the horizon.
        QcDimensionDefinition end = unfogged;
        end.sky = QcDimensionDefinition::Sky::End;
        const QcSkyState vault = QcSky::forDimension(end, night);
        QVERIFY(!vault.celestial);
        QVERIFY(vault.zenith.length() < vault.horizon.length());
        QVERIFY(vault.horizon.length() < 0.2f);
    }

    void onlyASunCastsShadows()
    {
        QcDimensionDefinition overworldSky = QcDimensionDefinition::defaultOverworld();
        overworldSky.dayCycle = false;
        QVERIFY(QcShadowCascades::light(QcSky::forDimension(overworldSky, 0.0)).strength > 0.5f);
        QCOMPARE(QcShadowCascades::light(QcSky::forDimension(netherLike(), 0.0)).strength, 0.0f);
    }

    void dialsSpinUnevenly()
    {
        // In [0, 360), moving, and not as a steady turn: the step between two moments changes.
        QList<double> steps;
        double previous = QcDials::spinAngle(0.0, QcDials::Dial::Compass);
        for (int i = 1; i <= 40; ++i) {
            const double angle = QcDials::spinAngle(i * 0.25, QcDials::Dial::Compass);
            QVERIFY(angle >= 0.0 && angle < 360.0);
            steps.append(std::remainder(angle - previous, 360.0));
            previous = angle;
        }
        const auto [low, high] = std::ranges::minmax(steps);
        QVERIFY2(high - low > 30.0, qPrintable(u"steps %1 .. %2"_s.arg(low).arg(high)));
        QVERIFY(low < 0.0); // now and then it swings back
        // Compass and clock do not turn alike.
        QVERIFY(std::abs(QcDials::spinAngle(3.0, QcDials::Dial::Compass) - QcDials::spinAngle(3.0, QcDials::Dial::Clock)) > 1.0);
    }

    void theClientForgetsTheOldDimension()
    {
        QTemporaryDir dir;
        const std::shared_ptr<const QcContentRegistry> content = contentWithDimensions();
        QcLocalServerHost host(configIn(dir.path()), content, std::make_unique<QcTestTerrainGenerator>(content));
        QVERIFY2(host.startError().isEmpty(), qPrintable(host.startError()));
        QcClientSession session(host.connectClient(), u"Traveller"_s);
        QSignalSpy changes(&session, &QcClientSession::dimensionChanged);
        session.start();
        QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Playing && session.world()->chunkCount() >= 9, 20000);
        // The join brings the dimension, after the content.
        QCOMPARE(changes.count(), 1);
        QCOMPARE(session.worldMirror().dimension().name, QString::fromLatin1(overworld));

        // A creature next to him and rain in the overworld.
        session.sendChat(u"/summon cow"_s);
        session.sendChat(u"/weather rain"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!session.entities().empty() && session.worldMirror().weather().rain() > 0.5f, 20000);
        const QList<QcChunkPos> before = session.world()->chunkPositions();

        // Nothing of the overworld is left the moment the change is reported: no chunk, no
        // creature, no rain (the new dimension's chunks may follow in the same pass).
        struct Seen
        {
            qsizetype chunks = -1;
            qsizetype entities = -1;
            float rain = -1.0f;
        };
        Seen seen;
        connect(&session, &QcClientSession::dimensionChanged, this, [&session, &seen] {
            seen = {session.world()->chunkCount(), session.entities().size(), session.worldMirror().weather().rain()};
        });
        session.sendChat(u"/tp 0.5 3 0.5 in test:empty"_s);
        QTRY_COMPARE_WITH_TIMEOUT(changes.count(), 2, 20000);
        QCOMPARE(session.worldMirror().dimension().name, QString::fromLatin1(emptyName));
        QVERIFY(!session.worldMirror().dimension().weather);
        QCOMPARE(seen.chunks, 0);
        QCOMPARE(seen.entities, 0);
        QCOMPARE(seen.rain, 0.0f);

        // The empty dimension arrives around him: its platform, and air where the overworld had ground.
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(session.playerMotion().position.y - 3.0) < 2.5 && session.world()->chunkCount() >= 9, 20000);
        const QcBlockId stone = *content->blockId(u"qtcraft:stone"_s);
        QCOMPARE(session.world()->blockAt({0, 1, 0}), std::optional<QcBlockId>(stone));
        for (const QcChunkPos &chunk : before) {
            if (const std::shared_ptr<const QcChunk> loaded = session.world()->chunk(chunk); loaded && (chunk.x != 0 || chunk.z != 0)) {
                QCOMPARE(session.world()->blockAt({chunk.x * QcWorld::chunkSize, 0, chunk.z * QcWorld::chunkSize}), std::optional<QcBlockId>(qcAirBlockId));
            }
        }
        QCOMPARE(session.worldMirror().weather().rain(), 0.0f);
        QVERIFY(session.entities().isEmpty());

        // Back home: the overworld again, with its creature and its rain.
        session.sendChat(u"/tp 0.5 80 0.5 in qtcraft:overworld"_s);
        QTRY_COMPARE_WITH_TIMEOUT(changes.count(), 3, 20000);
        QCOMPARE(session.worldMirror().dimension().name, QString::fromLatin1(overworld));
        QTRY_VERIFY_WITH_TIMEOUT(!session.entities().empty() && session.worldMirror().weather().rain() > 0.5f, 20000);
        QVERIFY(host.shutdown().has_value());
    }

    void playersInDifferentDimensionsDoNotSeeEachOther()
    {
        QTemporaryDir dir;
        const std::shared_ptr<const QcContentRegistry> content = contentWithDimensions();
        QcLocalServerHost host(configIn(dir.path()), content, std::make_unique<QcTestTerrainGenerator>(content));
        QVERIFY2(host.startError().isEmpty(), qPrintable(host.startError()));
        QcClientSession traveller(host.connectClient(), u"Traveller"_s);
        QcClientSession stayer(host.connectClient(), u"Stayer"_s);
        traveller.start();
        stayer.start();
        QTRY_VERIFY_WITH_TIMEOUT(traveller.state() == QcClientSession::State::Playing && stayer.state() == QcClientSession::State::Playing, 20000);
        const quint32 travellerId = traveller.welcome().playerEntityId;
        const quint32 stayerId = stayer.welcome().playerEntityId;
        QTRY_VERIFY_WITH_TIMEOUT(sees(traveller, stayerId) && sees(stayer, travellerId), 20000);

        // In the nether-like dimension he is gone for the other, and the other for him; both stay
        // on the player list.
        traveller.sendChat(u"/tp 0.5 3 0.5 in test:nether"_s);
        QTRY_COMPARE_WITH_TIMEOUT(traveller.worldMirror().dimension().name, QString::fromLatin1(netherName), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!sees(stayer, travellerId), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(traveller.playerMotion().position.y - 3.0) < 2.5, 20000);
        QVERIFY(!sees(traveller, stayerId));
        QCOMPARE(traveller.players().size(), 2);
        QCOMPARE(stayer.players().size(), 2);
        QCOMPARE(stayer.worldMirror().dimension().name, QString::fromLatin1(overworld));

        // The other follows him with /tp <player>: they meet again there.
        stayer.sendChat(u"/tp Traveller"_s);
        QTRY_COMPARE_WITH_TIMEOUT(stayer.worldMirror().dimension().name, QString::fromLatin1(netherName), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(sees(traveller, stayerId) && sees(stayer, travellerId), 20000);
        QVERIFY(host.shutdown().has_value());
    }
};

QTEST_GUILESS_MAIN(TestDimensionClient)
#include "test_dimension_client.moc"
