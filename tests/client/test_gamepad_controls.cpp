#include "app/controllers/QcHandbookController.h"
#include "app/controllers/QcScreenState.h"
#include "app/controllers/QcSettingsController.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/input/QcGameInput.h"
#include "app/models/QcItemIcons.h"
#include "app/session/QcSessionParts.h"
#include "audio/QcAudioEngine.h"
#include "clientcore/input/QcGamepadControls.h"
#include "clientcore/input/QcVirtualGamepadSource.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/loader/QcBaseContent.h"
#include "server/core/QcLocalServerHost.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <QQuickWindow>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30000;
constexpr float frameSeconds = 0.016f;

} // namespace

// The gamepad controls in the game's own order (decision E-541): every frame the keyboard writes
// the intent first (QcGameInput::applyFrame), then the pad (QcGamepadControls::applyFrame) with
// QcGameInput::worldFocused, as QcFrameDriver does. A virtual pad, a session against an in-process
// server and an offscreen window; the client never ticks, the frames alone change the intent.
class TestGamepadControls : public QObject
{
    Q_OBJECT

private:
    QcSessionParts m_parts;
    std::unique_ptr<QcAudioEngine> m_audio;
    std::unique_ptr<QcWorldLibraryController> m_library;
    std::unique_ptr<QcSettingsController> m_settings;
    QcItemIcons m_icons;
    std::unique_ptr<QcHandbookController> m_handbook;
    std::unique_ptr<QcScreenState> m_screens;
    std::unique_ptr<QcGameInput> m_input;
    std::unique_ptr<QQuickWindow> m_window;
    std::unique_ptr<QQuickWindow> m_other;
    QcVirtualGamepadSource m_source;
    QcGamepadControls m_pad;
    int m_first = 0;

    QcClientSession &session() const { return *m_parts.session; }

    // One frame as QcFrameDriver runs it.
    void frame()
    {
        m_input->applyFrame({}, frameSeconds);
        m_pad.applyFrame(session(), frameSeconds, m_input->worldFocused());
    }

    bool activate(QQuickWindow &window)
    {
        window.requestActivate();
        return QTest::qWaitForWindowActive(&window);
    }

    void axis(int pad, QcGamepadAxis axis, float value) { QVERIFY(m_source.setAxis(pad, axis, value)); }
    void press(int pad, QcGamepadButton button, bool pressed) { QVERIFY(m_source.setPressed(pad, button, pressed)); }

private slots:
    void initTestCase()
    {
        QStringList warnings;
        std::shared_ptr<const QcContentRegistry> content = *QcBaseContent::load(warnings);
        QcServerConfig config;
        config.viewDistance = 2;
        config.worldName = u"GamepadFocus"_s;
        m_parts.server = std::make_unique<QcLocalServerHost>(config, content, std::make_unique<QcTestTerrainGenerator>(content));
        m_parts.session = std::make_unique<QcClientSession>(m_parts.server->connectClient(), u"Pad"_s);
        session().start(QcClientSession::TickMode::Manual);
        QTRY_VERIFY_WITH_TIMEOUT(session().state() == QcClientSession::State::Playing && session().world(), timeoutMs);

        m_audio = std::make_unique<QcAudioEngine>();
        m_library = std::make_unique<QcWorldLibraryController>();
        m_settings = std::make_unique<QcSettingsController>(m_parts, *m_library, m_audio.get());
        m_handbook = std::make_unique<QcHandbookController>(m_parts, *m_settings, *m_library, m_icons);
        m_screens = std::make_unique<QcScreenState>(m_parts);
        m_input = std::make_unique<QcGameInput>(QcGameInput::Screens{*m_screens, *m_handbook}, m_parts, *m_settings, m_audio.get());
        m_input->setBackgroundRun(true);

        m_window = std::make_unique<QQuickWindow>();
        m_window->resize(320, 200);
        m_window->show();
        m_parts.window = m_window.get();
        m_other = std::make_unique<QQuickWindow>();
        m_other->resize(320, 200);
        m_other->setPosition(400, 0);
        m_other->show();

        m_pad.setSource(&m_source);
        m_first = m_source.connectPad(u"First"_s, QcGamepadType::Xbox);
        QCOMPARE(m_pad.activePad(), m_first);
    }

    void init()
    {
        QVERIFY(activate(*m_window));
        m_screens->setPhase(QcScreenState::Phase::Playing);
        m_screens->setOverlay({});
        // Looking straight up, so the trigger never hits a creature.
        QcPlayerInput intent = session().intent();
        intent.pitch = -89.0f;
        session().setIntent(intent);
        frame();
    }

    void cleanup()
    {
        m_input->holdKeys({});
        m_source.setState(m_first, {});
        frame();
        frame();
        if (m_pad.sneakToggled()) {
            press(m_first, QcGamepadButton::RightStick, true);
            press(m_first, QcGamepadButton::RightStick, false);
            frame();
        }
        session().setMining(false);
    }

    void cleanupTestCase()
    {
        m_pad.setSource(nullptr);
        m_input.reset();
        m_parts.window = nullptr;
        m_window.reset();
        m_other.reset();
        m_parts.session.reset();
        QVERIFY(m_parts.server->shutdown().has_value());
        m_parts.server.reset();
    }

    // The world takes the pad only while it is shown without a screen and its window is active.
    void worldFocusedFollowsScreensPhaseAndWindow()
    {
        QVERIFY(m_input->worldFocused());
        m_screens->setOverlay(u"pause"_s);
        QVERIFY(!m_input->worldFocused());
        m_screens->setOverlay({});
        QVERIFY(m_input->worldFocused());
        m_screens->setPhase(QcScreenState::Phase::Loading);
        QVERIFY(!m_input->worldFocused());
        m_screens->setPhase(QcScreenState::Phase::Playing);
        QVERIFY(m_input->worldFocused());
        QVERIFY(activate(*m_other));
        QVERIFY(!m_input->worldFocused());
    }

    // Keys first, then the pad: a deflected stick wins over the held key, a centred one gives the
    // walk back once and leaves the key's value from the next frame on.
    void padAfterKeyboardInFrameOrder()
    {
        m_input->holdKeys({Qt::Key_W});
        frame();
        QCOMPARE(session().intent().forward, 1.0f);

        axis(m_first, QcGamepadAxis::LeftY, 1.0f);
        frame();
        QCOMPARE(session().intent().forward, -1.0f);

        axis(m_first, QcGamepadAxis::LeftY, 0.0f);
        frame();
        frame();
        QCOMPARE(session().intent().forward, 1.0f);

        m_input->holdKeys({});
        frame();
        QCOMPARE(session().intent().forward, 0.0f);
    }

    // A screen over the world gives back walking and mining; a toggled sneak stays on and holds
    // again once the screen closes.
    void screenGivesBackButKeepsToggledSneak()
    {
        axis(m_first, QcGamepadAxis::LeftY, -1.0f);
        axis(m_first, QcGamepadAxis::RightTrigger, 1.0f);
        press(m_first, QcGamepadButton::RightStick, true);
        press(m_first, QcGamepadButton::RightStick, false);
        frame();
        QVERIFY(session().intent().forward > 0.9f);
        QVERIFY(session().intent().sneak);
        QVERIFY(session().miningRequested());

        m_screens->setOverlay(u"inventory"_s);
        frame();
        QCOMPARE(session().intent().forward, 0.0f);
        QVERIFY(!session().intent().sneak);
        QVERIFY(!session().miningRequested());
        QVERIFY(m_pad.sneakToggled());

        m_screens->setOverlay({});
        frame();
        QVERIFY(session().intent().sneak);
        // The trigger still held from before the screen does not mine again without a new press.
        QVERIFY(!session().miningRequested());
    }

    // Presses while the window is in the background (suspend(), as the game calls it when the
    // window loses the focus; no frames while minimised) are dropped, not run on return.
    void backgroundPressesAreDropped()
    {
        const int slot = session().inventory().selectedHotbarSlot();
        m_pad.suspend(m_parts.session.get());
        press(m_first, QcGamepadButton::RightShoulder, true);
        press(m_first, QcGamepadButton::RightShoulder, false);
        QCOMPARE(m_pad.pendingPresses(), qsizetype(0));

        frame();
        QCOMPARE(session().inventory().selectedHotbarSlot(), slot);
    }

    // Between two frames at most maxPresses presses wait; the oldest go first.
    void pressQueueIsBounded()
    {
        for (int click = 0; click < 200; ++click) {
            press(m_first, QcGamepadButton::RightShoulder, true);
            press(m_first, QcGamepadButton::RightShoulder, false);
        }
        QCOMPARE(m_pad.pendingPresses(), QcGamepadControls::maxPresses);
        frame();
        QCOMPARE(m_pad.pendingPresses(), qsizetype(0));
    }

    // Releasing the trigger ends only the pad's own mining, not the mouse's.
    void triggerReleaseLeavesMouseMining()
    {
        axis(m_first, QcGamepadAxis::RightTrigger, 1.0f);
        m_screens->setOverlay(u"pause"_s);
        frame();
        m_screens->setOverlay({});
        frame();
        session().setMining(true);
        axis(m_first, QcGamepadAxis::RightTrigger, 0.0f);
        frame();
        QVERIFY(session().miningRequested());
    }

    // A second pad takes the lead with a press, not with a drifting stick nor a pushed one while the
    // leading pad walks; the leader's held actions are let go when it hands over.
    void leadSwitchReleasesHeldActions()
    {
        const int second = m_source.connectPad(u"Second"_s, QcGamepadType::PlayStation);
        axis(m_first, QcGamepadAxis::LeftY, -1.0f);
        axis(m_first, QcGamepadAxis::RightTrigger, 1.0f);
        frame();
        QVERIFY(session().miningRequested());

        axis(second, QcGamepadAxis::LeftX, 0.3f);
        axis(second, QcGamepadAxis::RightX, 0.9f);
        frame();
        QCOMPARE(m_pad.activePad(), m_first);
        QVERIFY(session().intent().forward > 0.9f);

        press(second, QcGamepadButton::South, true);
        QCOMPARE(m_pad.activePad(), second);
        frame();
        QVERIFY(!session().miningRequested());
        QCOMPARE(session().intent().forward, 0.0f);
        press(second, QcGamepadButton::South, false);

        // Back to the first pad: a clearly pushed stick takes the lead from a resting one.
        axis(second, QcGamepadAxis::LeftX, 0.0f);
        axis(second, QcGamepadAxis::RightX, 0.0f);
        axis(m_first, QcGamepadAxis::LeftY, -0.9f);
        QCOMPARE(m_pad.activePad(), m_first);
        QVERIFY(m_source.disconnectPad(second));
    }
};

QTEST_MAIN(TestGamepadControls)
#include "test_gamepad_controls.moc"
