#include "clientcore/control/QcControlInterface.h"
#include "clientcore/input/QcGamepadResponse.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/loader/QcBaseContent.h"
#include "server/core/QcLocalServerHost.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <cmath>
#include <functional>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30000;

} // namespace

// The gamepad steering the player (decision E-541), played through the control interface's
// virtual pad against an in-process server: walking with stepless speed, looking, the hotbar on
// the shoulders, sprint and sneak on the stick clicks, mining and using with the triggers and
// creative flight by a double tap of jump. One world for all cases; each case connects its own pad.
// The client ticks by hand (TickMode::Manual), as fast as the test asks: the pads act once per
// client tick and the player moves by prediction. The server takes a player's inputs only at the
// pace of real time (input budget, E-249), so a burst of moving ticks is corrected afterwards;
// settle() gives it the time where the server's view matters (blocks next to the player, flight).
class TestGamepadScenarios : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QcLocalServerHost> m_host;
    std::unique_ptr<QcClientSession> m_session;
    std::unique_ptr<QBuffer> m_output;
    std::unique_ptr<QcControlInterface> m_control;
    qint64 m_readOffset = 0;
    int m_pad = 0;

    // Sends `command` and waits for its response line; `ticking` ticks the client while waiting
    // (commands that wait for ticks, such as chat).
    QJsonObject request(const QJsonObject &command, bool ticking = false)
    {
        m_control->submit(command);
        QJsonObject response;
        const bool answered = QTest::qWaitFor(
            [&] {
                if (ticking) {
                    m_session->tick();
                }
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

    QJsonObject status() { return request({{u"cmd"_s, u"status"_s}}); }

    // `count` client ticks at once; the server's messages arrive afterwards.
    void ticks(int count)
    {
        for (int tick = 0; tick < count; ++tick) {
            m_session->tick();
        }
        QCoreApplication::processEvents();
    }

    // `count` client ticks at the server's pace (20 per second): its view of the player catches up.
    void ticksInRealTime(int count)
    {
        for (int tick = 0; tick < count; ++tick) {
            m_session->tick();
            QTest::qWait(50);
        }
    }

    // Ticks at the server's pace until `done` holds, at most `limit` ticks; whether it came.
    bool ticksInRealTimeUntil(const std::function<bool()> &done, int limit)
    {
        for (int tick = 0; tick < limit; ++tick) {
            if (done()) {
                return true;
            }
            ticksInRealTime(1);
        }
        return done();
    }

    void settle() { ticksInRealTime(10); }

    bool flying() const { return m_session->playerMotion().flying; }
    void chat(const QString &text) { request({{u"cmd"_s, u"chat"_s}, {u"text"_s, text}}, true); }

    bool setPad(const QJsonObject &buttons, const QJsonObject &axes = {})
    {
        return request({{u"cmd"_s, u"gamepadSet"_s}, {u"pad"_s, m_pad}, {u"buttons"_s, buttons}, {u"axes"_s, axes}}).value(u"ok"_s).toBool();
    }

    // A click: pressed, a tick later released.
    bool click(const QString &button)
    {
        const bool pressed = setPad({{button, true}});
        ticks(1);
        return pressed && setPad({{button, false}});
    }

    QcVec3 position() const { return m_session->playerMotion().position; }

    // Metres walked south (+z) by prediction while the left stick is held at `stickY` for
    // `count` ticks; no server message (a correction of an earlier burst) comes in between.
    double walkSouth(float stickY, int count)
    {
        setPad({}, {{u"leftY"_s, stickY}});
        const double start = position().z;
        for (int tick = 0; tick < count; ++tick) {
            m_session->tick();
        }
        const double end = position().z;
        setPad({}, {{u"leftY"_s, 0.0}});
        ticks(2);
        return end - start;
    }

private slots:
    void initTestCase()
    {
        QStringList warnings;
        std::shared_ptr<const QcContentRegistry> content = *QcBaseContent::load(warnings);
        QcServerConfig config;
        config.viewDistance = 2;
        config.worldName = u"Gamepad"_s;
        m_host = std::make_unique<QcLocalServerHost>(config, content, std::make_unique<QcTestTerrainGenerator>(content));
        m_session = std::make_unique<QcClientSession>(m_host->connectClient(), u"Bot"_s);
        m_output = std::make_unique<QBuffer>();
        m_output->open(QIODevice::ReadWrite);
        m_control = std::make_unique<QcControlInterface>(m_session.get(), nullptr, m_output.get());
        m_session->start(QcClientSession::TickMode::Manual);
        // The player falls onto the ground at the server's pace.
        QVERIFY(QTest::qWaitFor(
            [&] {
                m_session->tick();
                return m_session->state() == QcClientSession::State::Playing && m_session->playerMotion().onGround;
            },
            timeoutMs));
        const QJsonObject ready = request({{u"cmd"_s, u"waitUntilReady"_s}});
        QVERIFY2(ready.value(u"ok"_s).toBool(), QJsonDocument(ready).toJson().constData());
        settle();
    }

    void init()
    {
        request({{u"cmd"_s, u"look"_s}, {u"yaw"_s, 180}, {u"pitch"_s, 0}});
        const QJsonObject connected = request({{u"cmd"_s, u"gamepadConnect"_s}, {u"type"_s, u"xbox"_s}});
        QVERIFY(connected.value(u"ok"_s).toBool());
        m_pad = connected.value(u"pad"_s).toInt();
        QVERIFY(m_pad > 0);
    }

    void cleanup()
    {
        QVERIFY(request({{u"cmd"_s, u"gamepadDisconnect"_s}, {u"pad"_s, m_pad}}).value(u"ok"_s).toBool());
        ticks(2);
    }

    void cleanupTestCase()
    {
        m_control.reset();
        m_session.reset();
        QVERIFY(m_host->shutdown().has_value());
        m_host.reset();
    }

    // The left stick walks; half a push walks as much slower as the curve says, released it stops.
    void leftStickWalksSteplessly()
    {
        const double full = walkSouth(-1.0f, 20);
        QVERIFY2(full > 2.5, qPrintable(QString::number(full)));
        QCOMPARE(m_session->intent().forward, 0.0f);

        const QcStickVector shaped = QcGamepadCurve::stick({0.0f, -0.5f}, QcGamepadResponse());
        setPad({}, {{u"leftY"_s, -0.5}});
        ticks(2);
        QCOMPARE(m_session->intent().forward, -shaped.y);
        QCOMPARE(m_session->intent().strafe, 0.0f);
        setPad({}, {{u"leftY"_s, 0.0}});
        ticks(2);

        const double half = walkSouth(-0.5f, 20);
        const double ratio = half / full;
        QVERIFY2(ratio > 0.15 && ratio < 0.5, qPrintable(u"%1 of %2 m"_s.arg(half).arg(full)));

        // Inside the dead zone the stick does nothing.
        const double resting = walkSouth(-0.1f, 10);
        QVERIFY2(std::abs(resting) < 0.05, qPrintable(QString::number(resting)));
    }

    // The right stick turns at the look speed (180 degrees per second by default) and looks up when pushed up.
    void rightStickLooks()
    {
        const double yaw = status().value(u"yaw"_s).toDouble();
        setPad({}, {{u"rightX"_s, 1.0}});
        ticks(10);
        setPad({}, {{u"rightX"_s, 0.0}});
        ticks(1);
        const double turned = status().value(u"yaw"_s).toDouble() - yaw;
        QVERIFY2(turned > 70.0 && turned < 120.0, qPrintable(QString::number(turned)));

        setPad({}, {{u"rightY"_s, -1.0}});
        ticks(3);
        setPad({}, {{u"rightY"_s, 0.0}});
        ticks(1);
        QVERIFY(status().value(u"pitch"_s).toDouble() > 10.0);
    }

    // The shoulders step through the hotbar and wrap around.
    void shouldersStepThroughTheHotbar()
    {
        QVERIFY(request({{u"cmd"_s, u"select"_s}, {u"slot"_s, 0}}).value(u"ok"_s).toBool());
        QVERIFY(click(u"rightShoulder"_s));
        ticks(2);
        // The server's answer to an earlier choice may still be on its way and set the slot back
        // for a moment; the last answer names the last choice.
        QTRY_COMPARE_WITH_TIMEOUT(status().value(u"selectedSlot"_s).toInt(), 1, timeoutMs);
        QVERIFY(click(u"leftShoulder"_s));
        QVERIFY(click(u"leftShoulder"_s));
        ticks(2);
        QTRY_COMPARE_WITH_TIMEOUT(status().value(u"selectedSlot"_s).toInt(), 8, timeoutMs);
    }

    // A click of the left stick sprints while walking forward; the right stick toggles sneaking,
    // which ends the sprint, and a second click stands up again.
    void stickClicksSprintAndSneak()
    {
        setPad({}, {{u"leftY"_s, -1.0}});
        ticks(2);
        QVERIFY(click(u"leftStick"_s));
        ticks(3);
        QVERIFY(m_session->intent().sprint);

        QVERIFY(click(u"rightStick"_s));
        ticks(2);
        QVERIFY(m_session->intent().sneak);
        QVERIFY(!m_session->intent().sprint);

        QVERIFY(click(u"rightStick"_s));
        ticks(2);
        QVERIFY(!m_session->intent().sneak);
        setPad({}, {{u"leftY"_s, 0.0}});
        ticks(5);
        QVERIFY(!m_session->intent().sprint);
        QCOMPARE(m_session->intent().forward, 0.0f);
    }

    // The right trigger breaks the block in view (creative: at once), the left one places the held block.
    void triggersBreakAndUse()
    {
        settle();
        chat(u"/gamemode creative"_s);
        chat(u"/setblock ~2 ~ ~ dirt"_s);
        const QcVec3 feet = position();
        const int x = int(std::floor(feet.x)) + 2;
        const int y = int(std::floor(feet.y));
        const int z = int(std::floor(feet.z));
        const auto blockAt = [&] { return request({{u"cmd"_s, u"block"_s}, {u"x"_s, x}, {u"y"_s, y}, {u"z"_s, z}}).value(u"block"_s).toString(); };
        QVERIFY(QTest::qWaitFor([&] { return blockAt() == u"qtcraft:dirt"_s; }, timeoutMs));
        request({{u"cmd"_s, u"lookAt"_s}, {u"x"_s, x}, {u"y"_s, y}, {u"z"_s, z}});
        ticks(2);

        setPad({}, {{u"rightTrigger"_s, 1.0}});
        ticks(3);
        setPad({}, {{u"rightTrigger"_s, 0.0}});
        QVERIFY2(QTest::qWaitFor([&] { return blockAt() == u"qtcraft:air"_s; }, timeoutMs), qPrintable(blockAt()));

        // Placing needs a face to place against: the block below the broken one.
        QVERIFY(request({{u"cmd"_s, u"select"_s}, {u"slot"_s, 0}}).value(u"ok"_s).toBool());
        chat(u"/give stone 1"_s);
        QVERIFY(QTest::qWaitFor([&] { return status().value(u"held"_s).toObject().value(u"item"_s).toString() == u"qtcraft:stone"_s; }, timeoutMs));
        request({{u"cmd"_s, u"lookAt"_s}, {u"x"_s, x}, {u"y"_s, y - 1}, {u"z"_s, z}});
        ticks(2);
        setPad({}, {{u"leftTrigger"_s, 1.0}});
        ticks(2);
        setPad({}, {{u"leftTrigger"_s, 0.0}});
        QVERIFY2(QTest::qWaitFor([&] { return blockAt() == u"qtcraft:stone"_s; }, timeoutMs), qPrintable(blockAt()));
        chat(u"/gamemode survival"_s);
    }

    // Two quick presses of south fly in creative mode, the held right stick click lands again. The
    // one case in real time: the double tap is timed on the wall clock (QcFlightLatch, 300 ms), and
    // the server has to follow the rising player, or its correction would land him at once.
    void doubleTapJumpFliesInCreative()
    {
        chat(u"/gamemode creative"_s);
        QVERIFY(QTest::qWaitFor([&] { return status().value(u"gameMode"_s).toString() == u"creative"_s; }, timeoutMs));
        settle();
        // A single press only jumps.
        QVERIFY(click(u"south"_s));
        ticksInRealTime(12);
        QVERIFY(!flying());

        // The second press is held a little, so the player rises off the ground (landing ends a flight).
        QVERIFY(setPad({{u"south"_s, true}}));
        QVERIFY(setPad({{u"south"_s, false}}));
        QVERIFY(setPad({{u"south"_s, true}}));
        ticksInRealTime(6);
        QVERIFY(setPad({{u"south"_s, false}}));
        QVERIFY(ticksInRealTimeUntil([this] { return flying(); }, 20));
        ticksInRealTime(5);
        QVERIFY(flying());

        // In flight the held right stick click descends until the player lands, which ends the flight.
        QVERIFY(setPad({{u"rightStick"_s, true}}));
        QVERIFY(ticksInRealTimeUntil([this] { return !flying(); }, 100));
        QVERIFY(setPad({{u"rightStick"_s, false}}));
        chat(u"/gamemode survival"_s);
    }

    // A frame without the focus (the game's window in the background, where SDL still reports the
    // pads) drops the presses since the last frame and gives back what the pad holds.
    void framesWithoutFocusDropPresses()
    {
        QVERIFY(request({{u"cmd"_s, u"select"_s}, {u"slot"_s, 0}}).value(u"ok"_s).toBool());
        setPad({}, {{u"leftY"_s, -1.0}});
        ticks(2);
        QVERIFY(m_session->intent().forward > 0.9f);

        QVERIFY(setPad({{u"rightShoulder"_s, true}}));
        m_control->gamepadControls().applyFrame(*m_session, 0.05f, false);
        QCOMPARE(m_session->intent().forward, 0.0f);
        ticks(3);
        QCOMPARE(status().value(u"selectedSlot"_s).toInt(), 0);
        QVERIFY(setPad({{u"rightShoulder"_s, false}}, {{u"leftY"_s, 0.0}}));
        ticks(2);
    }

    // Disconnecting the pad while it walks and mines gives both back.
    void disconnectingReleasesEverything()
    {
        setPad({}, {{u"leftY"_s, -1.0}, {u"rightTrigger"_s, 1.0}});
        ticks(3);
        QVERIFY(m_session->intent().forward > 0.9f);
        QVERIFY(request({{u"cmd"_s, u"gamepadDisconnect"_s}, {u"pad"_s, m_pad}}).value(u"ok"_s).toBool());
        ticks(2);
        QCOMPARE(m_session->intent().forward, 0.0f);
        // cleanup() disconnects the pad of the next case; this one is gone already.
        const QJsonObject connected = request({{u"cmd"_s, u"gamepadConnect"_s}, {u"type"_s, u"xbox"_s}});
        m_pad = connected.value(u"pad"_s).toInt();
    }
};

QTEST_GUILESS_MAIN(TestGamepadScenarios)
#include "test_gamepad_scenarios.moc"
