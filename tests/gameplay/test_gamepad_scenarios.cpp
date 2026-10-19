#include "clientcore/control/QcControlInterface.h"
#include "clientcore/input/QcGamepadResponse.h"
#include "clientcore/input/QcVirtualGamepadSource.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcInventory.h"
#include "QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "server/storage/QcPlayerStorage.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worlds/QcLocalServerHost.h"

#include <QBuffer>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
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
    // The world on disk, so a player who leaves comes back as he left (joiningHurtDoesNotVibrate).
    QTemporaryDir m_worldDirectory;
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

    // A fall of ten blocks; whether it hurt.
    bool fall()
    {
        const double before = m_session->vitals().health();
        const QcVec3 feet = position();
        chat(u"/tp %1 %2 %3"_s.arg(feet.x).arg(feet.y + 10.0).arg(feet.z));
        return ticksInRealTimeUntil([&] { return m_session->vitals().health() < before; }, 120);
    }

    // The pad is used (the device in use): a step to the left on the hotbar and back.
    void usePad()
    {
        QVERIFY(click(u"leftShoulder"_s));
        QVERIFY(click(u"rightShoulder"_s));
        ticks(1);
        QVERIFY(m_control->gamepadControls().padInUse());
    }

    // Before the session plays: a press and release, so the pad is the device in use.
    void usePadBeforeJoin()
    {
        QVERIFY(setPad({{u"leftShoulder"_s, true}}));
        QVERIFY(setPad({{u"leftShoulder"_s, false}}));
    }

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
        std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
        QcServerConfig config;
        config.viewDistance = 2;
        config.worldName = u"Gamepad"_s;
        QVERIFY(m_worldDirectory.isValid());
        config.worldDirectory = m_worldDirectory.path();
        config.autosaveSeconds = 0;
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

    // The shoulders step through the hotbar and wrap around. Each step shows at once and stays: the
    // server's answers to the earlier steps, arriving in between, never take the hand back to an
    // older slot (decision E-582).
    void shouldersStepThroughTheHotbar()
    {
        QVERIFY(request({{u"cmd"_s, u"select"_s}, {u"slot"_s, 0}}).value(u"ok"_s).toBool());
        settle();
        const auto selected = [this] { return m_session->inventory().selectedHotbarSlot(); };
        for (int step = 1; step <= 4; ++step) {
            QVERIFY(click(u"rightShoulder"_s));
            ticks(1);
            QCOMPARE(selected(), step);
        }
        for (int tick = 0; tick < 10; ++tick) {
            ticksInRealTime(1);
            QCOMPARE(selected(), 4);
        }
        QCOMPARE(status().value(u"selectedSlot"_s).toInt(), 4);

        for (int step = 1; step <= 5; ++step) {
            QVERIFY(click(u"leftShoulder"_s));
            ticks(1);
            QCOMPARE(selected(), (4 - step + 9) % 9);
        }
        settle();
        QCOMPARE(selected(), 8);
        QCOMPARE(status().value(u"selectedSlot"_s).toInt(), 8);
    }

    // Start, north, the d-pad and back ask for the screens like their keys (decision E-580); the
    // player list shows while back is held.
    void buttonsAskForScreens()
    {
        QSignalSpy actions(&m_control->gamepadControls(), &QcGamepadControls::worldAction);
        const QList<std::pair<QString, QcKeyBindings::Action>> buttons{
            {u"start"_s, QcKeyBindings::Action::Pause},     {u"north"_s, QcKeyBindings::Action::Inventory},
            {u"dpadDown"_s, QcKeyBindings::Action::Chat},   {u"dpadRight"_s, QcKeyBindings::Action::Handbook},
            {u"dpadUp"_s, QcKeyBindings::Action::CameraView},
        };
        for (const auto &[button, action] : buttons) {
            actions.clear();
            QVERIFY(click(button));
            ticks(1);
            QCOMPARE(actions.size(), 1);
            QCOMPARE(actions.first().at(0).value<QcKeyBindings::Action>(), action);
            QCOMPARE(actions.first().at(1).toBool(), true);
        }

        actions.clear();
        QVERIFY(setPad({{u"back"_s, true}}));
        ticks(1);
        QVERIFY(setPad({{u"back"_s, false}}));
        ticks(1);
        QCOMPARE(actions.size(), 2);
        QCOMPARE(actions.at(0).at(0).value<QcKeyBindings::Action>(), QcKeyBindings::Action::PlayerList);
        QCOMPARE(actions.at(0).at(1).toBool(), true);
        QCOMPARE(actions.at(1).at(1).toBool(), false);
    }

    // Back + Y saves a screenshot and back + X hides the HUD (decision E-692): the player list shows
    // while back is held, and Y and X do not also ask for their own actions; alone Y still opens the
    // inventory (above).
    void backCombosRunThePhotoActions()
    {
        using Key = QcKeyBindings::Action;
        QSignalSpy actions(&m_control->gamepadControls(), &QcGamepadControls::worldAction);
        const QList<QJsonObject> steps{
            {{u"back"_s, true}},
            {{u"back"_s, true}, {u"north"_s, true}},
            {{u"back"_s, true}, {u"north"_s, false}},
            {{u"back"_s, true}, {u"west"_s, true}},
            {{u"back"_s, true}, {u"west"_s, false}},
            {{u"back"_s, false}},
        };
        for (const QJsonObject &step : steps) {
            QVERIFY(setPad(step));
            ticks(1);
        }

        QList<std::pair<Key, bool>> seen;
        for (const QList<QVariant> &action : std::as_const(actions)) {
            seen.append({action.at(0).value<Key>(), action.at(1).toBool()});
        }
        const QList<std::pair<Key, bool>> expected{
            {Key::PlayerList, true}, {Key::Screenshot, true}, {Key::HideHud, true}, {Key::PlayerList, false}};
        QCOMPARE(seen, expected);
    }

    // Holding the radial button opens the menu on the slot in hand; the right stick points at a
    // slot instead of looking, and letting go selects it (decision E-581).
    void radialMenuSelectsWithTheRightStick()
    {
        QVERIFY(request({{u"cmd"_s, u"select"_s}, {u"slot"_s, 4}}).value(u"ok"_s).toBool());
        settle();
        QcGamepadControls &pad = m_control->gamepadControls();
        QSignalSpy changed(&pad, &QcGamepadControls::radialChanged);
        QVERIFY(setPad({{u"dpadLeft"_s, true}}));
        ticks(1);
        QVERIFY(pad.radialOpen());
        QCOMPARE(pad.radialSlot(), 4);

        const float yaw = m_session->intent().yaw;
        QVERIFY(setPad({}, {{u"rightX"_s, 1.0}}));
        ticks(3);
        QCOMPARE(pad.radialSlot(), 2);
        QCOMPARE(m_session->intent().yaw, yaw);
        // Back to the centre keeps the slot pointed at.
        QVERIFY(setPad({}, {{u"rightX"_s, 0.0}}));
        ticks(1);
        QCOMPARE(pad.radialSlot(), 2);

        QVERIFY(setPad({{u"dpadLeft"_s, false}}));
        ticks(1);
        QVERIFY(!pad.radialOpen());
        QCOMPARE(m_session->inventory().selectedHotbarSlot(), 2);
        QVERIFY(changed.size() >= 3);
        settle();
        QCOMPARE(status().value(u"selectedSlot"_s).toInt(), 2);

        // A frame without the focus closes it without choosing.
        QVERIFY(setPad({{u"dpadLeft"_s, true}}, {{u"rightX"_s, -1.0}}));
        ticks(2);
        QCOMPARE(pad.radialSlot(), 7);
        pad.applyFrame(*m_session, 0.05f, false);
        QVERIFY(!pad.radialOpen());
        QVERIFY(setPad({{u"dpadLeft"_s, false}}, {{u"rightX"_s, 0.0}}));
        ticks(2);
        QCOMPARE(m_session->inventory().selectedHotbarSlot(), 2);

        // Let go while the stick still points: it looks again only after it was back in the centre.
        QVERIFY(setPad({{u"dpadLeft"_s, true}}, {{u"rightX"_s, 1.0}}));
        ticks(2);
        QVERIFY(setPad({{u"dpadLeft"_s, false}}));
        ticks(3);
        QVERIFY(!pad.radialOpen());
        QCOMPARE(m_session->intent().yaw, yaw);
        QVERIFY(setPad({}, {{u"rightX"_s, 0.0}}));
        ticks(1);
        QVERIFY(setPad({}, {{u"rightX"_s, 1.0}}));
        ticks(2);
        QVERIFY(m_session->intent().yaw != yaw);
        QVERIFY(setPad({}, {{u"rightX"_s, 0.0}}));
    }

    // East throws one item, held for half a second the rest of the stack; west takes the block in
    // view into the hand in creative mode (decision E-580).
    void eastDropsAndWestPicksTheBlock()
    {
        // Five cobblestone in a slot of their own, in the hand.
        const QcItemId cobblestone = *m_session->content()->itemId(u"qtcraft:cobblestone"_s);
        const auto cobblestoneSlot = [&] {
            for (int slot = 0; slot < QcInventory::hotbarSize; ++slot) {
                if (m_session->inventory().slot(slot).item == cobblestone && m_session->inventory().slot(slot).count > 0) {
                    return slot;
                }
            }
            return -1;
        };
        QCOMPARE(cobblestoneSlot(), -1);
        chat(u"/give cobblestone 5"_s);
        QVERIFY(QTest::qWaitFor([&] { return cobblestoneSlot() >= 0; }, timeoutMs));
        QVERIFY(request({{u"cmd"_s, u"select"_s}, {u"slot"_s, cobblestoneSlot()}}).value(u"ok"_s).toBool());
        const auto heldCount = [this] { return status().value(u"held"_s).toObject().value(u"count"_s).toInt(); };
        QVERIFY(QTest::qWaitFor([&] { return heldCount() == 5; }, timeoutMs));

        QVERIFY(click(u"east"_s));
        ticks(1);
        QVERIFY2(QTest::qWaitFor([&] { return heldCount() == 4; }, timeoutMs), qPrintable(QString::number(heldCount())));
        QVERIFY(setPad({{u"east"_s, true}}));
        ticks(1);
        QTest::qWait(int(QcGamepadMapping::dropStackMs) + 50);
        ticks(1);
        QVERIFY(setPad({{u"east"_s, false}}));
        QVERIFY2(QTest::qWaitFor([&] { return heldCount() == 0; }, timeoutMs), qPrintable(QString::number(heldCount())));

        chat(u"/gamemode creative"_s);
        QVERIFY(QTest::qWaitFor([&] { return status().value(u"gameMode"_s).toString() == u"creative"_s; }, timeoutMs));
        chat(u"/setblock ~2 ~ ~ oak_planks"_s);
        const QcVec3 feet = position();
        const int x = int(std::floor(feet.x)) + 2;
        const int y = int(std::floor(feet.y));
        const int z = int(std::floor(feet.z));
        QVERIFY(QTest::qWaitFor([&] {
            return request({{u"cmd"_s, u"block"_s}, {u"x"_s, x}, {u"y"_s, y}, {u"z"_s, z}}).value(u"block"_s).toString() == u"qtcraft:oak_planks"_s;
        }, timeoutMs));
        request({{u"cmd"_s, u"lookAt"_s}, {u"x"_s, x}, {u"y"_s, y}, {u"z"_s, z}});
        // The picked block goes into the last slot, out of the way of the later cases.
        QVERIFY(request({{u"cmd"_s, u"select"_s}, {u"slot"_s, 8}}).value(u"ok"_s).toBool());
        ticks(2);
        QVERIFY(click(u"west"_s));
        ticks(1);
        QVERIFY2(QTest::qWaitFor([&] { return status().value(u"held"_s).toObject().value(u"item"_s).toString() == u"qtcraft:oak_planks"_s; }, timeoutMs),
                 qPrintable(status().value(u"held"_s).toObject().value(u"item"_s).toString()));
        chat(u"/gamemode survival"_s);
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

    // A fall that hurts vibrates the leading pad (decision E-615); with vibration off in its
    // profile, while the keyboard or mouse were used last, or while the window lacks the focus, it
    // does not.
    void hurtVibratesThePad()
    {
        QcGamepadControls &controls = m_control->gamepadControls();
        const QcVirtualGamepadSource &pads = m_control->virtualGamepads();
        QCOMPARE(controls.activePad(), m_pad);
        usePad();

        QVERIFY(fall());
        ticks(1);
        const QList<QcVirtualGamepadSource::Rumble> rumbles = pads.rumbles(m_pad);
        QCOMPARE(rumbles.size(), 1);
        QVERIFY(rumbles.first().strength > 0.35f && rumbles.first().strength <= 1.0f);
        QCOMPARE(rumbles.first().durationMs, QcGamepadControls::rumbleMs);
        chat(u"/heal"_s);

        QcGamepadSettings quiet;
        quiet.standard.vibration = false;
        controls.setSettings(quiet);
        QVERIFY(fall());
        ticks(1);
        QCOMPARE(pads.rumbles(m_pad).size(), 1);
        controls.setSettings({});
        chat(u"/heal"_s);

        controls.keyboardOrMouseUsed();
        QVERIFY(fall());
        ticks(1);
        QCOMPARE(pads.rumbles(m_pad).size(), 1);
        chat(u"/heal"_s);

        usePad();
        controls.setWindowActive(false);
        QVERIFY(fall());
        ticks(1);
        QCOMPARE(pads.rumbles(m_pad).size(), 1);
        controls.setWindowActive(true);
        chat(u"/heal"_s);
    }

    // A player who left hurt joins again with the pad in use: the vitals start full until the
    // server tells the health, and that first health is no damage (decision E-615). The frames
    // run from the start, as in the game once the world exists.
    void joiningHurtDoesNotVibrate()
    {
        QcGamepadControls &controls = m_control->gamepadControls();
        const QcVirtualGamepadSource &pads = m_control->virtualGamepads();
        usePad();
        QVERIFY(fall());
        ticks(1);
        const double hurt = m_session->vitals().health();
        QVERIFY(hurt < 18.0);
        const qsizetype rumbled = pads.rumbles(m_pad).size();

        m_control->setSession(nullptr);
        m_session.reset();
        // The server saves a player who leaves in the background: he comes back from that file.
        const QString saved = QDir(m_worldDirectory.path()).filePath(u"players/"_s + QcPlayerStorage::fileName(u"Bot"_s));
        QVERIFY(QTest::qWaitFor([&] { return QFileInfo::exists(saved); }, timeoutMs));
        m_session = std::make_unique<QcClientSession>(m_host->connectClient(), u"Bot"_s);
        m_control->setSession(m_session.get());
        usePadBeforeJoin();
        QVERIFY(controls.padInUse());
        QCOMPARE(m_session->vitals().health(), 20.0);

        m_session->start(QcClientSession::TickMode::Manual);
        QVERIFY(QTest::qWaitFor(
            [&] {
                controls.applyFrame(*m_session, 0.016f, false);
                m_session->tick();
                return m_session->state() == QcClientSession::State::Playing && m_session->vitals().healthKnown();
            },
            timeoutMs));
        ticks(2);
        QCOMPARE(m_session->vitals().health(), hurt);
        QCOMPARE(pads.rumbles(m_pad).size(), rumbled);

        QVERIFY(request({{u"cmd"_s, u"waitUntilReady"_s}}).value(u"ok"_s).toBool());
        settle();
        chat(u"/heal"_s);
    }
};

QTEST_GUILESS_MAIN(TestGamepadScenarios)
#include "test_gamepad_scenarios.moc"
