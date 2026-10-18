#include "clientcore/input/QcGamepadMapping.h"
#include "clientcore/settings/QcGamepadSettings.h"
#include "clientcore/settings/QcGameSettings.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

using namespace Qt::StringLiterals;
using QcGamepadMapping::Action;

namespace {

QcGamepadState stick(QcGamepadAxis x, float xValue, QcGamepadAxis y, float yValue)
{
    QcGamepadState state;
    state.setAxis(x, xValue);
    state.setAxis(y, yValue);
    return state;
}

} // namespace

// The arithmetic of the gamepad controls (decision E-541) and their settings: walking direction,
// turn per frame, triggers as buttons with hysteresis, default bindings, hotbar steps, and the
// gamepad values in settings.ini.
class TestGamepadMapping : public QObject
{
    Q_OBJECT

private slots:
    void leftStickWalks()
    {
        QVERIFY(!QcGamepadMapping::walk(QcGamepadState()).moving());
        // Up (y negative) walks forward, right strafes right.
        QCOMPARE(QcGamepadMapping::walk(stick(QcGamepadAxis::LeftX, 0.0f, QcGamepadAxis::LeftY, -1.0f)), (QcGamepadMapping::Walk{1.0f, 0.0f}));
        QCOMPARE(QcGamepadMapping::walk(stick(QcGamepadAxis::LeftX, 0.5f, QcGamepadAxis::LeftY, 0.25f)), (QcGamepadMapping::Walk{-0.25f, 0.5f}));
        // A centred stick gives a positive zero, which compares as "not moving".
        const QcGamepadMapping::Walk centred = QcGamepadMapping::walk(QcGamepadState());
        QVERIFY(!std::signbit(centred.forward));
        // The right stick does not walk.
        QVERIFY(!QcGamepadMapping::walk(stick(QcGamepadAxis::RightX, 1.0f, QcGamepadAxis::RightY, 1.0f)).moving());
    }

    void rightStickTurnsAtTheLookSpeed()
    {
        const QcGamepadState right = stick(QcGamepadAxis::RightX, 1.0f, QcGamepadAxis::RightY, -0.5f);
        const QPointF delta = QcGamepadMapping::lookDelta(right, 180.0, 0.5);
        QCOMPARE(delta.x(), 90.0);
        // Up is like the mouse moving up: QcMouseLook raises the pitch for a negative y.
        QCOMPARE(delta.y(), -45.0);
        QVERIFY(QcGamepadMapping::lookDelta(QcGamepadState(), 180.0, 0.5).isNull());
        QVERIFY(QcGamepadMapping::lookDelta(right, std::numeric_limits<double>::infinity(), 0.5).isNull());
        // The left stick does not look.
        QVERIFY(QcGamepadMapping::lookDelta(stick(QcGamepadAxis::LeftX, 1.0f, QcGamepadAxis::LeftY, 1.0f), 180.0, 1.0).isNull());
    }

    // A stick takes the lead from another pad only when pushed beyond half (raw, a drift stays
    // below); a stick outside its dead zone keeps the leading pad's lead.
    void sticksForTheLead()
    {
        QVERIFY(!QcGamepadMapping::clearlyDeflected(QcGamepadState()));
        QVERIFY(!QcGamepadMapping::clearlyDeflected(stick(QcGamepadAxis::LeftX, 0.3f, QcGamepadAxis::LeftY, 0.3f)));
        QVERIFY(QcGamepadMapping::clearlyDeflected(stick(QcGamepadAxis::LeftX, 0.4f, QcGamepadAxis::LeftY, 0.4f)));
        QVERIFY(QcGamepadMapping::clearlyDeflected(stick(QcGamepadAxis::RightX, 0.0f, QcGamepadAxis::RightY, -0.6f)));
        QVERIFY(!QcGamepadMapping::clearlyDeflected(stick(QcGamepadAxis::LeftTrigger, 1.0f, QcGamepadAxis::RightTrigger, 1.0f)));

        QVERIFY(!QcGamepadMapping::deflected(QcGamepadState()));
        QVERIFY(QcGamepadMapping::deflected(stick(QcGamepadAxis::LeftX, 0.1f, QcGamepadAxis::LeftY, 0.0f)));
        QVERIFY(QcGamepadMapping::deflected(stick(QcGamepadAxis::RightX, 0.0f, QcGamepadAxis::RightY, 0.1f)));
        QVERIFY(!QcGamepadMapping::deflected(stick(QcGamepadAxis::LeftTrigger, 1.0f, QcGamepadAxis::RightTrigger, 1.0f)));
    }

    void triggersHaveHysteresis()
    {
        QVERIFY(!QcGamepadMapping::triggerHeld(0.0f, false));
        QVERIFY(!QcGamepadMapping::triggerHeld(0.5f, false));
        QVERIFY(QcGamepadMapping::triggerHeld(0.51f, false));
        QVERIFY(QcGamepadMapping::triggerHeld(0.4f, true));
        QVERIFY(QcGamepadMapping::triggerHeld(0.3f, true));
        QVERIFY(!QcGamepadMapping::triggerHeld(0.29f, true));
        QVERIFY(!QcGamepadMapping::triggerHeld(0.4f, false));
    }

    void defaultBindings()
    {
        const QcGamepadMapping::Bindings bindings;
        const auto onlyPressed = [](QcGamepadButton button) {
            QcGamepadState state;
            state.setPressed(button, true);
            return state;
        };

        QVERIFY(QcGamepadMapping::held(Action::Jump, onlyPressed(QcGamepadButton::South), bindings, false));
        QVERIFY(QcGamepadMapping::held(Action::Sprint, onlyPressed(QcGamepadButton::LeftStick), bindings, false));
        QVERIFY(QcGamepadMapping::held(Action::Sneak, onlyPressed(QcGamepadButton::RightStick), bindings, false));
        QVERIFY(QcGamepadMapping::held(Action::HotbarPrevious, onlyPressed(QcGamepadButton::LeftShoulder), bindings, false));
        QVERIFY(QcGamepadMapping::held(Action::HotbarNext, onlyPressed(QcGamepadButton::RightShoulder), bindings, false));
        QVERIFY(!QcGamepadMapping::held(Action::Jump, onlyPressed(QcGamepadButton::East), bindings, false));

        QcGamepadState triggers;
        triggers.setAxis(QcGamepadAxis::RightTrigger, 1.0f);
        QVERIFY(QcGamepadMapping::held(Action::Mine, triggers, bindings, false));
        QVERIFY(!QcGamepadMapping::held(Action::Use, triggers, bindings, false));
        triggers.setAxis(QcGamepadAxis::LeftTrigger, 0.4f);
        QVERIFY(QcGamepadMapping::held(Action::Use, triggers, bindings, true));
        QVERIFY(!QcGamepadMapping::held(Action::Use, triggers, bindings, false));
    }

    void hotbarWraps()
    {
        QCOMPARE(QcGamepadMapping::hotbarStep(0, -1, 9), 8);
        QCOMPARE(QcGamepadMapping::hotbarStep(8, 1, 9), 0);
        QCOMPARE(QcGamepadMapping::hotbarStep(3, 2, 9), 5);
        QCOMPARE(QcGamepadMapping::hotbarStep(3, -12, 9), 0);
        QCOMPARE(QcGamepadMapping::hotbarStep(3, 1, 0), 3);
    }

    void settingsRoundTrip()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(u"settings.ini"_s);

        QcGameSettings settings;
        QCOMPARE(QcGameSettings::load(path).gamepad, QcGamepadSettings());
        settings.gamepad.response.deadZone = 0.25f;
        settings.gamepad.response.outerZone = 0.1f;
        settings.gamepad.response.exponent = 2.0f;
        settings.gamepad.response.triggerDeadZone = 0.2f;
        settings.gamepad.lookSpeed = 240.0;
        settings.gamepad.invertLook = true;
        QVERIFY(settings.save(path).has_value());
        QCOMPARE(QcGameSettings::load(path).gamepad, settings.gamepad);
    }

    void wildSettingsAreClamped()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(u"settings.ini"_s);
        {
            QSettings file(path, QSettings::IniFormat);
            file.setValue(u"gamepad/deadZone"_s, 5.0);
            file.setValue(u"gamepad/curve"_s, u"steep"_s);
            file.setValue(u"gamepad/lookSpeed"_s, 1e9);
            file.setValue(u"gamepad/invertLook"_s, true);
        }

        const QcGamepadSettings loaded = QcGameSettings::load(path).gamepad;
        QCOMPARE(loaded.response, (QcGamepadResponse{5.0f, QcGamepadResponse().outerZone, QcGamepadResponse().exponent}.sanitized()));
        QCOMPARE(loaded.response.exponent, QcGamepadResponse().exponent);
        QCOMPARE(loaded.lookSpeed, QcGamepadSettings::maxLookSpeed);
        QVERIFY(loaded.invertLook);

        QCOMPARE(QcGamepadSettings::clampLookSpeed(std::numeric_limits<double>::quiet_NaN()), QcGamepadSettings::defaultLookSpeed);
        QCOMPARE(QcGamepadSettings::clampLookSpeed(1.0), QcGamepadSettings::minLookSpeed);
    }
};

QTEST_GUILESS_MAIN(TestGamepadMapping)
#include "test_gamepad_mapping.moc"
