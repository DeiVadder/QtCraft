#include "clientcore/input/QcGamepadControls.h"
#include "clientcore/input/QcGamepadGlyphs.h"
#include "clientcore/input/QcGamepadMapping.h"
#include "clientcore/input/QcPointerRadial.h"
#include "clientcore/input/QcVirtualGamepadSource.h"
#include "clientcore/settings/QcGamepadSettings.h"
#include "clientcore/settings/QcGameSettings.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using QcGamepadMapping::Action;
using Shape = QcGamepadGlyphs::Shape;

namespace {

const QString padGuid = u"030000005e040000ea02000000007801"_s;
const QString otherGuid = u"030000004c050000e60c000000006800"_s;

// A profile that differs from the default in every value.
QcGamepadProfile changedProfile()
{
    QcGamepadProfile profile;
    profile.response.deadZone = 0.25f;
    profile.response.outerZone = 0.1f;
    profile.response.exponent = 2.0f;
    profile.response.triggerDeadZone = 0.2f;
    profile.lookSpeed = 240.0;
    profile.invertLook = true;
    profile.triggerThreshold = 0.7f;
    profile.vibration = false;
    profile.bindings.bind(Action::Jump, QcGamepadButton::East);
    return profile;
}

} // namespace

// The gamepad settings of package 40.3 (decisions E-610 to E-614): profiles stored with the
// settings, one per pad model (GUID), the buttons rebound with conflicts swapped, the trigger
// point, button symbols per family and the hotbar's radial menu with the mouse.
class TestGamepadProfiles : public QObject
{
    Q_OBJECT

private slots:
    // Every value of the standard profile, the models' own profiles and the radial-menu switch
    // come back from the file as they were saved.
    void settingsRoundTrip()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(u"settings.ini"_s);

        QcGameSettings settings;
        settings.gamepad.standard = changedProfile();
        settings.gamepad.radialOnly = true;
        QcGamepadProfile own;
        own.lookSpeed = 90.0;
        own.bindings.bind(Action::Inventory, QcGamepadButton::DpadUp);
        settings.gamepad.setProfile(padGuid, u"Xbox Wireless Controller"_s, own);
        settings.gamepad.setProfile(otherGuid, u"DualSense"_s, QcGamepadProfile());
        QVERIFY(settings.save(path).has_value());

        const QcGamepadSettings loaded = QcGameSettings::load(path).gamepad;
        QCOMPARE(loaded, settings.gamepad);
        QCOMPARE(loaded.controllers.size(), 2);
        QCOMPARE(loaded.controllers.first().name, u"Xbox Wireless Controller"_s);
        QCOMPARE(loaded.profileFor(padGuid).bindings.inventory, QcGamepadButton::DpadUp);
        // The swap put camera view on north.
        QCOMPARE(loaded.profileFor(padGuid).bindings.cameraView, QcGamepadButton::North);

        // Saving again with fewer models leaves no stale entry behind.
        settings.gamepad.removeProfile(padGuid);
        QVERIFY(settings.save(path).has_value());
        QCOMPARE(QcGameSettings::load(path).gamepad.controllers.size(), 1);
    }

    // The file of 40.2 (no trigger point, vibration, buttons or models) loads with the defaults.
    void olderFilesKeepTheirValues()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(u"settings.ini"_s);
        {
            QSettings file(path, QSettings::IniFormat);
            file.setValue(u"gamepad/lookSpeed"_s, 300.0);
            file.setValue(u"gamepad/invertLook"_s, true);
        }

        const QcGamepadSettings loaded = QcGameSettings::load(path).gamepad;
        QCOMPARE(loaded.standard.lookSpeed, 300.0);
        QVERIFY(loaded.standard.invertLook);
        QCOMPARE(loaded.standard.triggerThreshold, QcGamepadProfile::defaultTriggerThreshold);
        QVERIFY(loaded.standard.vibration);
        QCOMPARE(loaded.standard.bindings, QcGamepadMapping::Bindings());
        QVERIFY(loaded.controllers.isEmpty());
        QVERIFY(!loaded.radialOnly);
    }

    // Values edited by hand are clamped; unknown button names keep their default, a table with
    // two actions on one button falls back to the defaults, models without a GUID or seen twice
    // are dropped.
    void wildFilesAreCleaned()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(u"settings.ini"_s);
        {
            QSettings file(path, QSettings::IniFormat);
            file.setValue(u"gamepad/triggerThreshold"_s, 7.0);
            file.setValue(u"gamepad/bind/jump"_s, u"turbo"_s);
            file.setValue(u"gamepad/bind/drop"_s, u"dpadUp"_s);
            file.setValue(u"gamepadControllers/size"_s, 3);
            file.setValue(u"gamepadControllers/1/guid"_s, padGuid);
            file.setValue(u"gamepadControllers/1/bind/inventory"_s, u"south"_s);
            file.setValue(u"gamepadControllers/2/guid"_s, padGuid);
            file.setValue(u"gamepadControllers/3/name"_s, u"nameless"_s);
        }

        const QcGamepadSettings loaded = QcGameSettings::load(path).gamepad;
        QCOMPARE(loaded.standard.triggerThreshold, QcGamepadProfile::maxTriggerThreshold);
        // drop on dpadUp clashes with the camera view: all buttons default.
        QCOMPARE(loaded.standard.bindings, QcGamepadMapping::Bindings());
        QCOMPARE(loaded.controllers.size(), 1);
        // inventory on south clashes with jump.
        QCOMPARE(loaded.profileFor(padGuid).bindings, QcGamepadMapping::Bindings());

        QCOMPARE(QcGamepadProfile::clampTriggerThreshold(std::numeric_limits<float>::quiet_NaN()), QcGamepadProfile::defaultTriggerThreshold);
        QCOMPARE(QcGamepadProfile::clampTriggerThreshold(0.0f), QcGamepadProfile::minTriggerThreshold);
    }

    // A model uses its own profile, every other one the standard profile; beyond maxControllers
    // the oldest model goes.
    void profilesByGuid()
    {
        QcGamepadSettings settings;
        settings.standard.lookSpeed = 200.0;
        QcGamepadProfile own;
        own.lookSpeed = 500.0;
        settings.setProfile(padGuid, u"Pad"_s, own);

        QCOMPARE(settings.profileFor(padGuid).lookSpeed, 500.0);
        QCOMPARE(settings.profileFor(otherGuid).lookSpeed, 200.0);
        QCOMPARE(settings.profileFor({}).lookSpeed, 200.0);
        QVERIFY(settings.controller(padGuid));
        QVERIFY(!settings.controller({}));
        // An empty GUID never gets a profile.
        settings.setProfile({}, u"None"_s, own);
        QCOMPARE(settings.controllers.size(), 1);

        for (int index = 0; index < QcGamepadSettings::maxControllers; ++index) {
            settings.setProfile(u"guid%1"_s.arg(index), u"Pad %1"_s.arg(index), own);
        }
        QCOMPARE(settings.controllers.size(), QcGamepadSettings::maxControllers);
        QVERIFY(!settings.controller(padGuid));
        QVERIFY(settings.controller(u"guid0"_s));
    }

    // Beyond maxControllers the profile changed longest ago goes: a change moves a model's entry
    // last, and the models of connected pads (`keep`) are never evicted (decision E-611).
    void profilesEvictTheLeastRecentlyChanged()
    {
        QcGamepadSettings settings;
        QcGamepadProfile own;
        for (int index = 0; index < QcGamepadSettings::maxControllers; ++index) {
            settings.setProfile(u"guid%1"_s.arg(index), u"Pad %1"_s.arg(index), own);
        }

        // guid0 changed again: guid1 is now the oldest.
        own.lookSpeed = 300.0;
        settings.setProfile(u"guid0"_s, u"Renamed"_s, own);
        QCOMPARE(settings.controllers.last().guid, u"guid0"_s);
        QCOMPARE(settings.controllers.last().name, u"Pad 0"_s);
        QCOMPARE(settings.controllers.size(), QcGamepadSettings::maxControllers);

        // guid1 and guid2 are connected: guid3 goes for the new model.
        settings.setProfile(padGuid, u"Pad"_s, own, {u"guid1"_s, u"guid2"_s});
        QCOMPARE(settings.controllers.size(), QcGamepadSettings::maxControllers);
        QVERIFY(settings.controller(u"guid1"_s));
        QVERIFY(settings.controller(u"guid2"_s));
        QVERIFY(!settings.controller(u"guid3"_s));
        QVERIFY(settings.controller(u"guid0"_s));
        QCOMPARE(settings.controllers.last().guid, padGuid);

        // Without a keep list the oldest goes.
        settings.setProfile(otherGuid, u"Other"_s, own);
        QVERIFY(!settings.controller(u"guid1"_s));
    }

    // Binding a button another action has swaps the two; triggers and Guide are refused; a table
    // with a button twice is found.
    void rebindingSwapsConflicts()
    {
        QcGamepadMapping::Bindings bindings;
        QCOMPARE(bindings.bind(Action::Drop, QcGamepadButton::South), std::optional(Action::Jump));
        QCOMPARE(bindings.drop, QcGamepadButton::South);
        QCOMPARE(bindings.jump, QcGamepadButton::East);
        QVERIFY(bindings.conflicts().isEmpty());

        // A free button or the action's own one moves nothing else.
        QCOMPARE(bindings.bind(Action::Drop, QcGamepadButton::South), std::nullopt);
        QCOMPARE(bindings.bind(Action::PlayerList, QcGamepadButton::Back), std::nullopt);

        const QcGamepadMapping::Bindings before = bindings;
        QCOMPARE(bindings.bind(Action::Mine, QcGamepadButton::North), std::nullopt);
        QCOMPARE(bindings.bind(Action::Jump, QcGamepadButton::Guide), std::nullopt);
        QCOMPARE(bindings, before);

        bindings.setButton(Action::Chat, QcGamepadButton::Start);
        QCOMPARE(bindings.conflicts(), (QList<Action>{Action::Chat}));
        QCOMPARE(bindings.action(QcGamepadButton::Start), std::optional(Action::Pause));
        QCOMPARE(QcGamepadMapping::Bindings().action(QcGamepadButton::Guide), std::nullopt);

        QVERIFY(!QcGamepadMapping::rebindable(Action::Mine));
        QVERIFY(QcGamepadMapping::rebindable(Action::HotbarRadial));
        for (std::size_t index = 0; index < QcGamepadMapping::actionCount; ++index) {
            const QString name = QcGamepadMapping::actionName(Action(index));
            QVERIFY(!name.isEmpty());
            QCOMPARE(QcGamepadMapping::actionFromName(name), std::optional(Action(index)));
        }
        QCOMPARE(QcGamepadMapping::actionFromName(u"fly"_s), std::nullopt);
    }

    // The photo mode's actions start without a button (decision E-692): no conflict, nothing held;
    // they cannot take the button of an action bound by default (pause on start), only a button no
    // such action needs; a profile keeps an action without a button.
    void photoActionsStartUnbound()
    {
        QcGamepadMapping::Bindings bindings;
        QCOMPARE(bindings.button(Action::HideHud), std::nullopt);
        QCOMPARE(bindings.button(Action::Screenshot), std::nullopt);
        QVERIFY(bindings.conflicts().isEmpty());
        QVERIFY(QcGamepadMapping::rebindable(Action::Screenshot));
        QVERIFY(!QcGamepadMapping::held(Action::HideHud, QcGamepadState(), bindings, false, 0.5f));

        const QcGamepadMapping::Bindings defaults = bindings;
        QCOMPARE(bindings.stranded(Action::Screenshot, QcGamepadButton::Start), std::optional(Action::Pause));
        QCOMPARE(bindings.bind(Action::Screenshot, QcGamepadButton::Start), std::nullopt);
        QCOMPARE(bindings.bind(Action::HideHud, QcGamepadButton::Back), std::nullopt);
        QCOMPARE(bindings, defaults);
        QCOMPARE(bindings.button(Action::Pause), std::optional(QcGamepadButton::Start));

        // An action bound by default may still swap with another one that has a button.
        QCOMPARE(bindings.stranded(Action::Chat, QcGamepadButton::Start), std::nullopt);

        // A button no default action needs any more (a profile file left it free) can be taken.
        bindings.setButton(Action::PlayerList, QcGamepadMapping::unbound);
        bindings.setButton(Action::Handbook, QcGamepadButton::Back);
        QCOMPARE(bindings.stranded(Action::Screenshot, QcGamepadButton::DpadRight), std::nullopt);
        QCOMPARE(bindings.bind(Action::Screenshot, QcGamepadButton::DpadRight), std::nullopt);
        QCOMPARE(bindings.button(Action::Screenshot), std::optional(QcGamepadButton::DpadRight));
        QCOMPARE(bindings.button(Action::PlayerList), std::nullopt);
        QVERIFY(bindings.conflicts().isEmpty());

        QTemporaryDir directory;
        const QString path = directory.filePath(u"pad.ini"_s);
        QcGamepadProfile profile;
        profile.bindings = bindings;
        {
            QSettings file(path, QSettings::IniFormat);
            profile.save(file, u"pad"_s);
        }
        const QSettings file(path, QSettings::IniFormat);
        QCOMPARE(file.value(u"pad/bind/playerList"_s).toString(), u"none"_s);
        QCOMPARE(QcGamepadProfile::load(file, u"pad"_s).bindings, bindings);
    }

    // Back held turns Y into the screenshot and X into hiding the HUD for the whole press (decision
    // E-692); Y pressed before Back stays the inventory; without Back, Y and X are their own.
    void backCombosReplaceTheButtonsOwnAction()
    {
        using Held = std::array<bool, QcGamepadMapping::actionCount>;
        const QcGamepadMapping::Bindings bindings;
        const auto step = [&bindings](const Held &before, std::initializer_list<QcGamepadButton> down) {
            QcGamepadState state;
            for (const QcGamepadButton button : down) {
                state.setPressed(button, true);
            }
            Held now{};
            for (std::size_t index = 0; index < now.size(); ++index) {
                now[index] = QcGamepadMapping::held(Action(index), state, bindings, before[index]);
            }
            QcGamepadMapping::applyCombos(now, before, state, bindings);
            return now;
        };
        const auto on = [](const Held &held, Action action) { return held[std::size_t(action)]; };

        const Held back = step({}, {QcGamepadButton::Back});
        QVERIFY(on(back, Action::PlayerList));
        const Held backY = step(back, {QcGamepadButton::Back, QcGamepadButton::North});
        QVERIFY(on(backY, Action::Screenshot));
        QVERIFY(!on(backY, Action::Inventory));
        QVERIFY(on(backY, Action::PlayerList));
        const Held yAfterBack = step(backY, {QcGamepadButton::North});
        QVERIFY(on(yAfterBack, Action::Screenshot));
        QVERIFY(!on(yAfterBack, Action::Inventory));

        const Held backX = step(back, {QcGamepadButton::Back, QcGamepadButton::West});
        QVERIFY(on(backX, Action::HideHud));
        QVERIFY(!on(backX, Action::PickBlock));

        const Held y = step({}, {QcGamepadButton::North});
        QVERIFY(on(y, Action::Inventory));
        const Held yThenBack = step(y, {QcGamepadButton::North, QcGamepadButton::Back});
        QVERIFY(on(yThenBack, Action::Inventory));
        QVERIFY(!on(yThenBack, Action::Screenshot));
        QVERIFY(on(step({}, {QcGamepadButton::West}), Action::PickBlock));

        QCOMPARE(QcGamepadMapping::comboOf(Action::Screenshot)->button, QcGamepadButton::North);
        QVERIFY(!QcGamepadMapping::comboOf(Action::Jump));
    }

    // A higher trigger point needs more pull and lets go 0.2 below it.
    void triggerThresholdMovesThePoint()
    {
        using QcGamepadMapping::triggerHeld;
        QVERIFY(triggerHeld(0.6f, false));
        QVERIFY(!triggerHeld(0.6f, false, 0.7f));
        QVERIFY(triggerHeld(0.75f, false, 0.7f));
        QVERIFY(triggerHeld(0.55f, true, 0.7f));
        QVERIFY(!triggerHeld(0.45f, true, 0.7f));
        QCOMPARE(QcGamepadMapping::releaseFor(0.1f), 0.05f);

        QcGamepadState state;
        state.setAxis(QcGamepadAxis::RightTrigger, 0.6f);
        QVERIFY(QcGamepadMapping::held(Action::Mine, state, {}, false, 0.5f));
        QVERIFY(!QcGamepadMapping::held(Action::Mine, state, {}, false, 0.7f));
    }

    // The controls take the profile of the leading pad's model, change it with the lead and act on
    // the rebound buttons of that pad.
    void controlsFollowTheLeadingPadsProfile()
    {
        QcGamepadSettings settings;
        QcGamepadProfile own;
        own.lookSpeed = 500.0;
        settings.setProfile(padGuid, u"Own"_s, own);

        QcVirtualGamepadSource source;
        QcGamepadControls controls;
        controls.setSettings(settings);
        QSignalSpy changed(&controls, &QcGamepadControls::profileChanged);
        controls.setSource(&source);
        const int plain = source.connectPad(u"Plain"_s, QcGamepadType::Generic);
        QCOMPARE(controls.activePad(), plain);
        QCOMPARE(controls.profile().lookSpeed, QcGamepadProfile::defaultLookSpeed);

        const int ownPad = source.connectPad(u"Own"_s, QcGamepadType::Xbox, padGuid);
        changed.clear();
        QVERIFY(source.setPressed(ownPad, QcGamepadButton::North, true));
        QCOMPARE(controls.activePad(), ownPad);
        QCOMPARE(controls.profile().lookSpeed, 500.0);
        QVERIFY(!changed.isEmpty());

        QVERIFY(source.disconnectPad(ownPad));
        QCOMPARE(controls.activePad(), 0);
        QCOMPARE(controls.profile().lookSpeed, QcGamepadProfile::defaultLookSpeed);
        QCOMPARE(source.info(plain)->guid, QString());
        QVERIFY(source.info(plain)->rumble);
        QVERIFY(!source.info(ownPad));
    }

    // The family follows the pad type; each family draws south and the d-pad as its pads label them.
    void glyphsFollowTheFamily()
    {
        using QcGamepadGlyphs::family;
        using QcGamepadGlyphs::glyph;
        QCOMPARE(family(QcGamepadType::Xbox), QcGamepadFamily::Xbox);
        QCOMPARE(family(QcGamepadType::PlayStation), QcGamepadFamily::PlayStation);
        QCOMPARE(family(QcGamepadType::SwitchPro), QcGamepadFamily::Nintendo);
        QCOMPARE(family(QcGamepadType::Generic), QcGamepadFamily::Generic);

        QCOMPARE(glyph(QcGamepadFamily::Xbox, u"south"_s), (QcGamepadGlyphs::Glyph{Shape::Label, u"A"_s, u"green"_s, {}}));
        QCOMPARE(glyph(QcGamepadFamily::PlayStation, u"south"_s)->shape, Shape::Cross);
        QCOMPARE(glyph(QcGamepadFamily::PlayStation, u"east"_s)->shape, Shape::Circle);
        QCOMPARE(glyph(QcGamepadFamily::PlayStation, u"west"_s)->shape, Shape::Square);
        QCOMPARE(glyph(QcGamepadFamily::PlayStation, u"north"_s)->shape, Shape::Triangle);
        QCOMPARE(glyph(QcGamepadFamily::Nintendo, u"south"_s)->label, u"B"_s);
        QCOMPARE(glyph(QcGamepadFamily::Nintendo, u"east"_s)->label, u"A"_s);
        QCOMPARE(glyph(QcGamepadFamily::Generic, u"south"_s), (QcGamepadGlyphs::Glyph{Shape::Position, {}, {}, u"down"_s}));
        QCOMPARE(glyph(QcGamepadFamily::Generic, u"dpadLeft"_s), (QcGamepadGlyphs::Glyph{Shape::Dpad, {}, {}, u"left"_s}));
        QCOMPARE(glyph(QcGamepadFamily::PlayStation, u"rightTrigger"_s)->label, u"R2"_s);
        QCOMPARE(glyph(QcGamepadFamily::Nintendo, u"leftTrigger"_s)->label, u"ZL"_s);
        QCOMPARE(glyph(QcGamepadFamily::Xbox, u"leftShoulder"_s), (QcGamepadGlyphs::Glyph{Shape::Shoulder, u"LB"_s, {}, {}}));
        QCOMPARE(glyph(QcGamepadFamily::Xbox, u"rightStickMove"_s)->shape, Shape::Stick);
        QCOMPARE(glyph(QcGamepadFamily::Xbox, u"turbo"_s), std::nullopt);

        // Every input has a glyph in every family.
        for (std::size_t button = 0; button < qcGamepadButtonCount; ++button) {
            for (const QcGamepadFamily each : {QcGamepadFamily::Generic, QcGamepadFamily::Xbox, QcGamepadFamily::PlayStation, QcGamepadFamily::Nintendo}) {
                QVERIFY(glyph(each, QcGamepadNames::button(QcGamepadButton(button))));
            }
        }
    }

    // Running text: letters, the PlayStation shapes as characters, arrows for the d-pad; the face
    // buttons of a generic pad are left to the caller.
    void glyphTexts()
    {
        using QcGamepadGlyphs::text;
        QCOMPARE(text(QcGamepadFamily::Xbox, u"north"_s), u"Y"_s);
        QCOMPARE(text(QcGamepadFamily::PlayStation, u"south"_s), u"✕"_s);
        QCOMPARE(text(QcGamepadFamily::PlayStation, u"start"_s), u"Options"_s);
        QCOMPARE(text(QcGamepadFamily::Nintendo, u"back"_s), u"−"_s);
        QCOMPARE(text(QcGamepadFamily::Generic, u"dpadDown"_s), u"▼"_s);
        QCOMPARE(text(QcGamepadFamily::Generic, u"south"_s), QString());
        QCOMPARE(text(QcGamepadFamily::Xbox, u"turbo"_s), QString());
        for (const QString &name : {u"generic"_s, u"xbox"_s, u"playstation"_s, u"nintendo"_s}) {
            QCOMPARE(QcGamepadGlyphs::familyName(*QcGamepadGlyphs::familyFromName(name)), name);
        }
        QCOMPARE(QcGamepadGlyphs::familyFromName(u"sega"_s), std::nullopt);
    }

    // The mouse moves a pointer out from the centre; its direction picks the slot, it stays within
    // reach, so turning back is quick; closing selects, cancelling does not.
    void pointerRadialChoosesByDirection()
    {
        QcPointerRadial radial(9);
        QVERIFY(!radial.move({200.0, 0.0}));
        radial.open(4);
        QCOMPARE(radial.highlighted(), 4);
        // Less than half the reach points at nothing.
        QVERIFY(!radial.move({0.0, -0.4 * QcPointerRadial::reach}));
        QCOMPARE(radial.highlighted(), 4);
        QVERIFY(radial.move({0.0, -0.4 * QcPointerRadial::reach}));
        QCOMPARE(radial.highlighted(), 0);

        // Far to the right: the pointer stops at the rim, so a move of twice the reach to the left
        // points left at once.
        radial.move({1000.0, 0.8 * QcPointerRadial::reach});
        QVERIFY(std::hypot(radial.pointer().x(), radial.pointer().y()) <= QcPointerRadial::reach + 1e-9);
        QCOMPARE(radial.highlighted(), 2);
        QVERIFY(radial.move({-2.0 * QcPointerRadial::reach, 0.0}));
        QCOMPARE(radial.highlighted(), 7);
        QCOMPARE(radial.close(), std::optional(7));
        QVERIFY(!radial.isOpen());
        QCOMPARE(radial.close(), std::nullopt);

        radial.open(1);
        radial.move({0.0, QcPointerRadial::reach});
        radial.cancel();
        QVERIFY(!radial.isOpen());
        QCOMPARE(radial.pointer(), QPointF());
    }
};

QTEST_GUILESS_MAIN(TestGamepadProfiles)
#include "test_gamepad_profiles.moc"
