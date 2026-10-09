#include "QcBaseContent.h"
#include "QcClientSession.h"
#include "QcControlInterface.h"
#include "QcLocalServerHost.h"
#include "QcTestTerrainGenerator.h"
#include "QcTransport.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30000;

} // namespace

// End-to-end scenarios through the control interface: server thread, protocol, client session.
class TestScenarios : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QcLocalServerHost> m_host;
    std::unique_ptr<QcClientSession> m_session;
    std::unique_ptr<QBuffer> m_output;
    std::unique_ptr<QcControlInterface> m_control;
    qint64 m_readOffset = 0;

    QJsonObject request(const QJsonObject &command)
    {
        m_control->submit(command);
        QJsonObject response;
        const bool answered = QTest::qWaitFor(
            [&] {
                const QByteArray &data = m_output->data();
                const qsizetype newline = data.indexOf('\n', m_readOffset);
                if (newline < 0) {
                    return false;
                }
                response = QJsonDocument::fromJson(data.mid(m_readOffset, newline - m_readOffset)).object();
                m_readOffset = newline + 1;
                return true;
            },
            timeoutMs);

        if (!answered) {
            qWarning("no response to %s", QJsonDocument(command).toJson(QJsonDocument::Compact).constData());
        }
        return response;
    }

    static QcVec3 position(const QJsonObject &status)
    {
        const QJsonArray p = status.value(u"position"_s).toArray();
        return {p.at(0).toDouble(), p.at(1).toDouble(), p.at(2).toDouble()};
    }

    // Selects the hotbar slot holding `item` (given before).
    bool select(const QString &item)
    {
        const QJsonArray list = request({{u"cmd"_s, u"inventory"_s}}).value(u"slots"_s).toArray();
        for (const auto &value : list) {
            const QJsonObject slot = value.toObject();
            if (slot.value(u"item"_s).toString() == item && slot.value(u"slot"_s).toInt() < 9) {
                return request({{u"cmd"_s, u"select"_s}, {u"slot"_s, slot.value(u"slot"_s).toInt()}}).value(u"ok"_s).toBool();
            }
        }
        return false;
    }

    int countOf(const QString &item)
    {
        int total = 0;
        const QJsonArray list = request({{u"cmd"_s, u"inventory"_s}}).value(u"slots"_s).toArray();
        for (const auto &value : list) {
            const QJsonObject slot = value.toObject();
            total += slot.value(u"item"_s).toString() == item ? slot.value(u"count"_s).toInt() : 0;
        }
        return total;
    }

private slots:
    void init()
    {
        QStringList warnings;
        std::shared_ptr<const QcContentRegistry> content = *QcBaseContent::load(warnings);
        QcServerConfig config;
        config.viewDistance = 2;
        config.worldName = u"Scenario"_s;
        m_host = std::make_unique<QcLocalServerHost>(config, content, std::make_unique<QcTestTerrainGenerator>(content));
        m_session = std::make_unique<QcClientSession>(m_host->connectClient(), u"Bot"_s);
        m_output = std::make_unique<QBuffer>();
        m_output->open(QIODevice::ReadWrite);
        m_control = std::make_unique<QcControlInterface>(m_session.get(), nullptr, m_output.get());
        m_readOffset = 0;
        m_session->start();
        const QJsonObject ready = request({{u"cmd"_s, u"waitUntilReady"_s}});
        QVERIFY2(ready.value(u"ok"_s).toBool(), QJsonDocument(ready).toJson().constData());
    }

    void cleanup()
    {
        m_control.reset();
        m_session.reset();
        QVERIFY(m_host->shutdown().has_value());
        m_host.reset();
    }

    void statusDescribesThePlayer()
    {
        const QJsonObject status = request({{u"cmd"_s, u"status"_s}, {u"id"_s, 7}});
        QCOMPARE(status.value(u"id"_s).toInt(), 7);
        QCOMPARE(status.value(u"state"_s).toString(), u"Playing"_s);
        QCOMPARE(status.value(u"world"_s).toString(), u"Scenario"_s);
        QVERIFY(status.value(u"onGround"_s).toBool());

        // Chunks are generated in the background; the full 5 x 5 square arrives shortly after.
        int loaded = 0;
        QVERIFY(QTest::qWaitFor(
            [&] {
                loaded = request({{u"cmd"_s, u"status"_s}}).value(u"loadedChunks"_s).toInt();
                return loaded == 25;
            },
            timeoutMs));
        QCOMPARE(loaded, 25);
    }

    void walkingSouthIncreasesZ()
    {
        const QcVec3 start = position(request({{u"cmd"_s, u"look"_s}, {u"yaw"_s, 180}}));
        const QJsonObject after = request({{u"cmd"_s, u"move"_s}, {u"forward"_s, 1}, {u"ticks"_s, 20}});
        QVERIFY(after.value(u"ok"_s).toBool());
        const QcVec3 end = position(after);
        QVERIFY2(end.z > start.z + 0.5, qPrintable(QString::number(end.z - start.z)));
        QVERIFY(std::abs(end.x - start.x) < 1e-9);
    }

    void jumpLeavesTheGroundAndLandsAgain()
    {
        const QJsonObject inAir = request({{u"cmd"_s, u"jump"_s}});
        QVERIFY(!inAir.value(u"onGround"_s).toBool());
        QVERIFY(position(inAir).y > 0.0);
        const QJsonObject landed = request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 30}});
        QVERIFY(landed.value(u"onGround"_s).toBool());
    }

    void blockQueriesSeeSolidGroundBelowThePlayer()
    {
        const QcVec3 feet = position(request({{u"cmd"_s, u"status"_s}}));
        const QJsonObject below = request({{u"cmd"_s, u"block"_s},
                                           {u"x"_s, int(std::floor(feet.x))},
                                           {u"y"_s, int(std::floor(feet.y)) - 1},
                                           {u"z"_s, int(std::floor(feet.z))}});
        const QString name = below.value(u"block"_s).toString();
        QVERIFY2(name == u"qtcraft:grass_block"_s || name == u"qtcraft:sand"_s, qPrintable(name));
        const QJsonObject around = request({{u"cmd"_s, u"blocks"_s}, {u"radius"_s, 1}});
        QVERIFY(!around.value(u"blocks"_s).toArray().isEmpty());
    }

    void soundsCanBeObserved()
    {
        // Walking makes the player's own steps; they are reported with their ground and origin.
        request({{u"cmd"_s, u"look"_s}, {u"yaw"_s, 180}});
        request({{u"cmd"_s, u"move"_s}, {u"forward"_s, 1}, {u"ticks"_s, 30}});

        const auto keysOf = [](const QJsonObject &response, const QString &origin) {
            QStringList keys;
            const QJsonArray sounds = response.value(u"sounds"_s).toArray();
            for (const auto &sound : sounds) {
                if (sound.toObject().value(u"origin"_s).toString() == origin) {
                    keys.append(sound.toObject().value(u"key"_s).toString());
                }
            }
            return keys;
        };

        const QJsonObject walked = request({{u"cmd"_s, u"sounds"_s}, {u"clear"_s, true}});
        QVERIFY(walked.value(u"ok"_s).toBool());
        const QStringList steps = keysOf(walked, u"local"_s);
        QVERIFY2(steps.contains(u"qtcraft:block.grass.step"_s) || steps.contains(u"qtcraft:block.sand.step"_s), qPrintable(steps.join(u", "_s)));

        // Cleared: nothing new has happened since.
        QVERIFY(request({{u"cmd"_s, u"sounds"_s}}).value(u"sounds"_s).toArray().isEmpty());

        // Mining a block: local strikes while mining, the break from the server.
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/setblock ~2 ~ ~ dirt"_s}});
        const QcVec3 feet = position(request({{u"cmd"_s, u"status"_s}}));
        const QJsonObject mined = request({{u"cmd"_s, u"mine"_s},
                                           {u"x"_s, int(std::floor(feet.x)) + 2},
                                           {u"y"_s, int(std::floor(feet.y))},
                                           {u"z"_s, int(std::floor(feet.z))}});
        QVERIFY2(mined.value(u"ok"_s).toBool(), QJsonDocument(mined).toJson().constData());
        request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 2}});
        const QJsonObject heard = request({{u"cmd"_s, u"sounds"_s}});
        QVERIFY(keysOf(heard, u"local"_s).contains(u"qtcraft:block.dirt.hit"_s));
        QVERIFY(keysOf(heard, u"server"_s).contains(u"qtcraft:block.dirt.break"_s));

        const QJsonArray heardSounds = heard.value(u"sounds"_s).toArray();
        for (const auto &value : heardSounds) {
            const QJsonObject sound = value.toObject();
            if (sound.value(u"key"_s).toString() == u"qtcraft:block.dirt.break"_s) {
                QVERIFY(sound.value(u"known"_s).toBool());
                QVERIFY(sound.value(u"distance"_s).toDouble() < 4.0);
                // Close by and in view: loud, near the centre.
                QVERIFY(sound.value(u"gain"_s).toDouble() > 0.3);
                QVERIFY(std::abs(sound.value(u"pan"_s).toDouble()) < 0.3);
            }
        }

        // Under the open sky the wind fades in, and there is no cave.
        const QJsonObject outside = request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 60}});
        QVERIFY(outside.value(u"ok"_s).toBool());
        const QJsonObject ambience = request({{u"cmd"_s, u"sounds"_s}}).value(u"ambience"_s).toObject();
        QVERIFY2(ambience.value(u"wind"_s).toDouble() >= 0.25, QJsonDocument(ambience).toJson().constData());
        QCOMPARE(ambience.value(u"cave"_s).toDouble(), 0.0);
    }

    void bucketsAndBottlesThroughTheInterface()
    {
        // An empty bucket aims at liquids: using it on water fills it.
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/fill ~1 ~ ~-1 ~3 ~3 ~1 air"_s}});
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/setblock ~2 ~-1 ~ water"_s}});
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/give bucket 1"_s}});
        QVERIFY(select(u"qtcraft:bucket"_s));

        const QcVec3 feet = position(request({{u"cmd"_s, u"status"_s}}));
        const QJsonObject used = request({{u"cmd"_s, u"use"_s},
                                          {u"x"_s, int(std::floor(feet.x)) + 2},
                                          {u"y"_s, int(std::floor(feet.y)) - 1},
                                          {u"z"_s, int(std::floor(feet.z))},
                                          {u"face"_s, u"up"_s}});
        QVERIFY2(used.value(u"ok"_s).toBool(), QJsonDocument(used).toJson().constData());
        request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 2}});
        QCOMPARE(countOf(u"qtcraft:water_bucket"_s), 1);

        // A firefly at night: caught with a glass bottle.
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/time set night"_s}});
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/give glass_bottle 1"_s}});
        QVERIFY(select(u"qtcraft:glass_bottle"_s));
        request({{u"cmd"_s, u"look"_s}, {u"yaw"_s, 0}, {u"pitch"_s, 0}});
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/summon firefly"_s}});
        request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 2}});

        qint64 firefly = -1;
        const QJsonArray entities = request({{u"cmd"_s, u"entities"_s}, {u"radius"_s, 8}}).value(u"entities"_s).toArray();
        for (const auto &value : entities) {
            if (value.toObject().value(u"type"_s).toString() == u"qtcraft:firefly"_s) {
                firefly = value.toObject().value(u"id"_s).toInteger();
            }
        }

        QVERIFY(firefly >= 0);
        const QJsonObject caught = request({{u"cmd"_s, u"useEntity"_s}, {u"entity"_s, firefly}});
        QVERIFY2(caught.value(u"ok"_s).toBool(), QJsonDocument(caught).toJson().constData());
        QCOMPARE(countOf(u"qtcraft:firefly_bottle"_s), 1);
        QCOMPARE(countOf(u"qtcraft:glass_bottle"_s), 0);
    }

    void viewModeCanBeSwitched()
    {
        QCOMPARE(request({{u"cmd"_s, u"view"_s}}).value(u"mode"_s).toString(), u"first"_s);
        QCOMPARE(request({{u"cmd"_s, u"view"_s}, {u"mode"_s, u"front"_s}}).value(u"mode"_s).toString(), u"front"_s);
        QCOMPARE(m_session->cameraMode(), QcCameraMode::Front);
        const QJsonObject wrong = request({{u"cmd"_s, u"view"_s}, {u"mode"_s, u"sideways"_s}});
        QCOMPARE(wrong.value(u"ok"_s).toBool(true), false);
        QCOMPARE(m_session->cameraMode(), QcCameraMode::Front);
        request({{u"cmd"_s, u"view"_s}, {u"mode"_s, u"first"_s}});
    }

    void errorsAreReportedNotFatal()
    {
        const QJsonObject unknown = request({{u"cmd"_s, u"fly"_s}, {u"id"_s, u"x"_s}});
        QCOMPARE(unknown.value(u"ok"_s).toBool(true), false);
        QCOMPARE(unknown.value(u"id"_s).toString(), u"x"_s);
        QCOMPARE(m_control->failedRequests(), 1);
        QVERIFY(request({{u"cmd"_s, u"status"_s}}).value(u"ok"_s).toBool());
    }

    void malformedLinesFromTheInputDeviceAreRejected()
    {
        QBuffer input;
        input.open(QIODevice::ReadWrite);
        QBuffer output;
        output.open(QIODevice::ReadWrite);
        QcControlInterface control(m_session.get(), &input, &output);
        input.write("not json\n{\"cmd\":\"status\"}\n");
        input.seek(0);
        emit input.readyRead();
        QTRY_VERIFY_WITH_TIMEOUT(output.data().count('\n') >= 2, timeoutMs);
        const QList<QByteArray> lines = output.data().split('\n');
        QCOMPARE(QJsonDocument::fromJson(lines.at(0)).object().value(u"ok"_s).toBool(true), false);
        QCOMPARE(QJsonDocument::fromJson(lines.at(1)).object().value(u"ok"_s).toBool(), true);
    }
};

QTEST_GUILESS_MAIN(TestScenarios)
#include "test_scenarios.moc"
