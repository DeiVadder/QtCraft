#include "app/controllers/QcGamepadOptions.h"
#include "app/controllers/QcHandbookController.h"
#include "app/controllers/QcScreenState.h"
#include "app/controllers/QcSettingsController.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/input/QcGameInput.h"
#include "app/input/QcGamepadRoster.h"
#include "app/input/QcGamepadUi.h"
#include "app/models/QcItemIcons.h"
#include "app/session/QcSessionParts.h"
#include "audio/QcAudioEngine.h"
#include "clientcore/input/QcGamepadControls.h"
#include "clientcore/input/QcVirtualGamepadSource.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worlds/QcLocalServerHost.h"

#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30000;
constexpr float frameSeconds = 0.016f;
const QString ownGuid = u"03000000de2800000112000000000000"_s;

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
        std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
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
        m_screens->setOverlay(QcScreenState::Overlay::None);
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
        m_screens->setOverlay(QcScreenState::Overlay::Pause);
        QVERIFY(!m_input->worldFocused());
        m_screens->setOverlay(QcScreenState::Overlay::None);
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

        m_screens->setOverlay(QcScreenState::Overlay::Inventory);
        frame();
        QCOMPARE(session().intent().forward, 0.0f);
        QVERIFY(!session().intent().sneak);
        QVERIFY(!session().miningRequested());
        QVERIFY(m_pad.sneakToggled());

        m_screens->setOverlay(QcScreenState::Overlay::None);
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
        m_screens->setOverlay(QcScreenState::Overlay::Pause);
        frame();
        m_screens->setOverlay(QcScreenState::Overlay::None);
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

    // A pad model with a profile of its own acts on its rebound buttons (decision E-611); other
    // pads keep the standard profile.
    void ownProfileRebindsTheModelsButtons()
    {
        QcGamepadSettings settings;
        QcGamepadProfile own;
        own.bindings.bind(QcGamepadMapping::Action::Jump, QcGamepadButton::East);
        settings.setProfile(ownGuid, u"Own"_s, own);
        m_pad.setSettings(settings);
        const int pad = m_source.connectPad(u"Own"_s, QcGamepadType::Xbox, ownGuid);

        press(pad, QcGamepadButton::East, true);
        QCOMPARE(m_pad.activePad(), pad);
        QCOMPARE(m_pad.profile().bindings.jump, QcGamepadButton::East);
        frame();
        QVERIFY(session().intent().jump);
        press(pad, QcGamepadButton::East, false);
        frame();
        QVERIFY(!session().intent().jump);

        // The first pad has no own profile: south jumps there.
        press(m_first, QcGamepadButton::South, true);
        QCOMPARE(m_pad.profile().bindings.jump, QcGamepadButton::South);
        frame();
        QVERIFY(session().intent().jump);
        press(m_first, QcGamepadButton::South, false);
        frame();
        QVERIFY(m_source.disconnectPad(pad));
        m_pad.setSettings({});
    }

    // The leading pad disconnecting in the world while it is in use asks for the pause menu
    // (decision E-613); on a screen it does not.
    void leadingPadDisconnectPauses()
    {
        QSignalSpy actions(&m_pad, &QcGamepadControls::worldAction);
        const int pad = m_source.connectPad(u"Leaving"_s, QcGamepadType::Xbox);
        press(pad, QcGamepadButton::West, true);
        press(pad, QcGamepadButton::West, false);
        frame();
        actions.clear();
        QVERIFY(m_source.disconnectPad(pad));
        QCOMPARE(actions.size(), 1);
        QCOMPARE(actions.first().at(0).value<QcKeyBindings::Action>(), QcKeyBindings::Action::Pause);
        QCOMPARE(m_pad.activePad(), 0);

        const int other = m_source.connectPad(u"Other"_s, QcGamepadType::Xbox);
        QCOMPARE(m_pad.activePad(), other);
        press(other, QcGamepadButton::West, true);
        press(other, QcGamepadButton::West, false);
        frame();
        QVERIFY(m_pad.padInUse());
        m_screens->setOverlay(QcScreenState::Overlay::Inventory);
        frame();
        actions.clear();
        QVERIFY(m_source.disconnectPad(other));
        QVERIFY(actions.isEmpty());
        m_screens->setOverlay(QcScreenState::Overlay::None);

        // Back to the first pad for the cases after this one.
        press(m_first, QcGamepadButton::West, true);
        press(m_first, QcGamepadButton::West, false);
        QCOMPARE(m_pad.activePad(), m_first);
    }

    // A leading pad that is not the device in use pauses nothing when it disconnects: one the
    // keyboard took over from, and one that connected and was never pressed (decision E-613).
    void idleLeadingPadDisconnectDoesNotPause()
    {
        QSignalSpy actions(&m_pad, &QcGamepadControls::worldAction);
        QcGamepadUi ui(m_pad, [] { return false; });
        ui.setSource(&m_source);
        press(m_first, QcGamepadButton::West, true);
        press(m_first, QcGamepadButton::West, false);
        frame();
        QCOMPARE(m_pad.activePad(), m_first);
        QVERIFY(m_pad.padInUse());
        QVERIFY(ui.active());

        QTest::keyClick(m_window.get(), Qt::Key_F20);
        QVERIFY(!ui.active());
        QVERIFY(!m_pad.padInUse());
        frame();
        actions.clear();
        QVERIFY(m_source.disconnectPad(m_first));
        QVERIFY(actions.isEmpty());
        QCOMPARE(m_pad.activePad(), 0);

        m_first = m_source.connectPad(u"First"_s, QcGamepadType::Xbox);
        QCOMPARE(m_pad.activePad(), m_first);
        QVERIFY(!m_pad.padInUse());
        frame();
        QVERIFY(m_source.disconnectPad(m_first));
        QVERIFY(actions.isEmpty());
        m_first = m_source.connectPad(u"First"_s, QcGamepadType::Xbox);
    }

    // The radial key opens the hotbar's radial menu on the slot in hand; letting go without moving
    // the mouse keeps it, a screen opening closes it (decision E-614).
    void radialKeyOpensTheMenu()
    {
        const int slot = session().inventory().selectedHotbarSlot();
        QSignalSpy changed(m_input.get(), &QcGameInput::radialChanged);
        QTest::keyPress(m_window.get(), Qt::Key_G);
        QVERIFY(m_input->radialOpen());
        QCOMPARE(m_input->radialSlot(), slot);
        QTest::keyRelease(m_window.get(), Qt::Key_G);
        QVERIFY(!m_input->radialOpen());
        QCOMPARE(session().inventory().selectedHotbarSlot(), slot);
        QCOMPARE(changed.size(), 2);

        QTest::keyPress(m_window.get(), Qt::Key_G);
        QVERIFY(m_input->radialOpen());
        m_screens->setOverlay(QcScreenState::Overlay::Pause);
        QVERIFY(!m_input->radialOpen());
        m_screens->setOverlay(QcScreenState::Overlay::None);
        QTest::keyRelease(m_window.get(), Qt::Key_G);
    }

    // The roster follows the pad in use: its family names and draws the buttons (a virtual
    // PlayStation pad gets the shapes), and connecting, disconnecting and taking over give notices
    // (decisions E-612, E-613).
    void rosterFollowsThePadInUse()
    {
        QcVirtualGamepadSource source;
        QcGamepadRoster roster;
        roster.setSource(&source);
        QCOMPARE(roster.family(), u"xbox"_s);
        QSignalSpy notices(&roster, &QcGamepadRoster::noticeChanged);

        const int xbox = source.connectPad(u"Xbox pad"_s, QcGamepadType::Xbox);
        const int playStation = source.connectPad(u"DualSense"_s, QcGamepadType::PlayStation, ownGuid);
        QCOMPARE(roster.inUse(), xbox);
        QCOMPARE(notices.size(), 2);
        QVERIFY(roster.notice().contains(u"DualSense"_s));

        QVERIFY(source.setPressed(playStation, QcGamepadButton::South, true));
        QCOMPARE(roster.inUse(), playStation);
        QCOMPARE(roster.family(), u"playstation"_s);
        QCOMPARE(roster.guid(), ownGuid);
        QCOMPARE(notices.size(), 3);
        QCOMPARE(roster.texts().value(u"south"_s).toString(), u"✕"_s);
        QCOMPARE(roster.text(u"rightStickMove"_s), QcGamepadRoster::tr("Right stick"));
        QCOMPARE(roster.glyph(roster.family(), u"north"_s).value(u"shape"_s).toString(), u"triangle"_s);
        QCOMPARE(roster.glyph(u"xbox"_s, u"north"_s).value(u"label"_s).toString(), u"Y"_s);
        QVERIFY(roster.glyph(roster.family(), u"turbo"_s).isEmpty());
        QCOMPARE(roster.list().size(), 2);
        QVERIFY(roster.list().at(1).toMap().value(u"inUse"_s).toBool());

        QVERIFY(source.disconnectPad(playStation));
        QCOMPARE(roster.inUse(), xbox);
        QCOMPARE(roster.family(), u"xbox"_s);
        QCOMPARE(notices.size(), 4);

        const int generic = source.connectPad(u"Unknown"_s, QcGamepadType::Generic);
        QVERIFY(source.setAxis(generic, QcGamepadAxis::RightTrigger, 1.0f));
        QCOMPARE(roster.family(), u"generic"_s);
        QCOMPARE(roster.texts().value(u"south"_s).toString(), QcGamepadRoster::tr("Bottom button"));
    }

    // While a button is captured for the settings the screens ignore the pad; the next press of a
    // bindable button ends it (Guide does not), and the world actions follow the profile.
    void captureTakesTheNextButton()
    {
        QcVirtualGamepadSource source;
        QcGamepadControls controls;
        QcGamepadUi ui(controls, [] { return true; });
        controls.setSource(&source);
        // The order of the source's handlers matters: ui.setSource() connects the screens' menu
        // input (QcGamepadMenuInput) before the capture (QcGamepadUi::onPadStateChanged), so the
        // press that ends a capture reaches the menu input while the capture still gates it, and
        // it never also acts on the screen (`commands` stays empty below).
        ui.setSource(&source);
        const int pad = source.connectPad(u"Pad"_s, QcGamepadType::Xbox);
        QSignalSpy captured(&ui, &QcGamepadUi::buttonCaptured);
        QSignalSpy commands(&ui.menuInput(), &QcGamepadMenuInput::command);

        // A button held when the capture starts is not taken.
        QVERIFY(source.setPressed(pad, QcGamepadButton::South, true));
        ui.captureButton();
        QVERIFY(ui.capturing());
        QVERIFY(source.setPressed(pad, QcGamepadButton::South, false));
        QVERIFY(source.setPressed(pad, QcGamepadButton::Guide, true));
        QVERIFY(captured.isEmpty());
        commands.clear();
        QVERIFY(source.setPressed(pad, QcGamepadButton::DpadUp, true));
        QCOMPARE(captured.size(), 1);
        QCOMPARE(captured.first().at(0).toString(), u"dpadUp"_s);
        QVERIFY(!ui.capturing());
        QVERIFY(commands.isEmpty());

        ui.captureButton();
        ui.cancelCapture();
        QVERIFY(!ui.capturing());

        // A pad that connects with a button held does not end the capture with it; the last pad
        // disconnecting ends it without a button.
        QVERIFY(source.setPressed(pad, QcGamepadButton::DpadUp, false));
        ui.captureButton();
        QcGamepadState held;
        held.setPressed(QcGamepadButton::North, true);
        const int late = source.connectPad(u"Late"_s, QcGamepadType::Xbox);
        QVERIFY(source.setState(late, held));
        QVERIFY(captured.size() == 1 && ui.capturing());
        QVERIFY(source.setPressed(late, QcGamepadButton::West, true));
        QCOMPARE(captured.size(), 2);
        QCOMPARE(captured.last().at(0).toString(), u"west"_s);
        ui.captureButton();
        QVERIFY(source.disconnectPad(late));
        QVERIFY(ui.capturing());
        QVERIFY(source.disconnectPad(pad));
        QVERIFY(!ui.capturing());
        QCOMPARE(captured.size(), 2);

        QCOMPARE(ui.actions().value(u"inventory"_s).toString(), u"north"_s);
        QCOMPARE(ui.actions().value(u"mine"_s).toString(), u"rightTrigger"_s);
        QcGamepadSettings settings;
        settings.standard.bindings.bind(QcGamepadMapping::Action::Inventory, QcGamepadButton::DpadUp);
        QSignalSpy actionsChanged(&ui, &QcGamepadUi::actionsChanged);
        controls.setSettings(settings);
        QCOMPARE(actionsChanged.size(), 1);
        QCOMPARE(ui.actions().value(u"inventory"_s).toString(), u"dpadUp"_s);
    }

    // The settings page opens on the profile of the pad in use when its model has one of its own;
    // removing the profile of a model that is not connected goes back to the standard one, and a
    // connected model stays edited (decision E-611).
    void optionsFollowThePadInUse()
    {
        QcVirtualGamepadSource source;
        QcGamepadOptions options;
        options.setSource(&source);
        const int pad = source.connectPad(u"Own"_s, QcGamepadType::Xbox, ownGuid);
        options.editProfileOf(ownGuid);
        QCOMPARE(options.profileKey(), QString());

        options.makeOwnProfile(ownGuid, u"Own"_s);
        options.makeOwnProfile(u"gone"_s, u"Gone"_s);
        options.setProfileKey({});
        options.editProfileOf(ownGuid);
        QCOMPARE(options.profileKey(), ownGuid);
        QVERIFY(options.ownProfile());

        options.removeOwnProfile();
        QCOMPARE(options.profileKey(), ownGuid);
        QVERIFY(!options.ownProfile());

        options.setProfileKey(u"gone"_s);
        QVERIFY(options.ownProfile());
        options.removeOwnProfile();
        QCOMPARE(options.profileKey(), QString());
        QVERIFY(options.editable());
        QVERIFY(options.settings().controllers.isEmpty());
        QVERIFY(source.disconnectPad(pad));
    }

    // Giving a photo action the start button is refused with a message naming the pause menu, so a
    // pad-only player can always pause (decision E-692); the profile stays as it was, and the row
    // shows the action's combination.
    void bindRefusesToStrandPause()
    {
        QcGamepadOptions options;
        QVERIFY(options.editable());
        const QcGamepadSettings before = options.settings();

        const QString message = options.bind(u"screenshot"_s, u"start"_s);
        QVERIFY2(message.contains(u"Pause menu"_s), qPrintable(message));
        QVERIFY2(message.startsWith(u"Not bound"_s), qPrintable(message));
        QCOMPARE(options.settings().standard.bindings, before.standard.bindings);
        QCOMPARE(options.settings().standard.bindings.button(QcGamepadMapping::Action::Pause),
                 std::optional(QcGamepadButton::Start));

        // The page shows the fixed combination instead: back + Y.
        const QVariantList rows = options.rows();
        for (const QVariant &value : rows) {
            const QVariantMap row = value.toMap();
            if (row.value(u"action"_s) == u"screenshot"_s) {
                QCOMPARE(row.value(u"comboModifier"_s).toString(), u"back"_s);
                QCOMPARE(row.value(u"comboButton"_s).toString(), u"north"_s);
                return;
            }
        }
        QFAIL("no row for the screenshot");
    }

    // A key or a click anywhere in the game cancels a capture, whatever has the focus, and does
    // nothing else (decision E-611).
    void keyOrClickCancelsCapture()
    {
        QcVirtualGamepadSource source;
        QcGamepadControls controls;
        QcGamepadUi ui(controls, [] { return true; });
        controls.setSource(&source);
        ui.setSource(&source);
        source.connectPad(u"Pad"_s, QcGamepadType::Xbox);
        QSignalSpy captured(&ui, &QcGamepadUi::buttonCaptured);

        ui.captureButton();
        QVERIFY(ui.capturing());
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QVERIFY(!ui.capturing());

        ui.captureButton();
        QTest::mouseClick(m_window.get(), Qt::LeftButton, {}, QPoint(10, 10));
        QVERIFY(!ui.capturing());
        QVERIFY(captured.isEmpty());

        // Without a capture (and the pad not in use) the filter is gone again.
        const quint64 calls = ui.hookCalls();
        QVERIFY(calls > 0);
        QTest::keyClick(m_window.get(), Qt::Key_F20);
        QCOMPARE(ui.hookCalls(), calls);
    }
};

QTEST_MAIN(TestGamepadControls)
#include "test_gamepad_controls.moc"
