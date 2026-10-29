pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtCraft

// The gamepad page of the settings (decisions E-610, E-611, E-613, E-614, E-845) in three groups:
// the profile (the one all controllers use, or a controller model's own; the controllers connected
// now, the one in use marked: pressing a button on another one hands it over), sticks and triggers
// (look speed, invert, dead zones, the response curve, the trigger point, vibration) and the
// buttons (the radial menu alone and the buttons of the actions). "Change" waits for the next
// button on any controller (a button used by another action is swapped); a key or a click anywhere
// (GameController.gamepad takes them), the last controller disconnecting or leaving the page
// cancels. The page opens on the profile of the controller in use when its model has its own.
SettingsPage {
    id: page

    required property GameController game
    readonly property GamepadOptions options: game.settings.gamepad
    // The action waiting for a button ("" while none does) and what the last change moved.
    property string captureAction

    // The profiles to choose from: all controllers, the connected ones, then the stored ones.
    readonly property var profileChoices: {
        const choices = [{ text: qsTr("All controllers"), value: "", name: "" }];
        const known = new Set([""]);
        for (const pad of game.gamepad.pads.list) {
            if (pad.guid.length > 0 && !known.has(pad.guid)) {
                known.add(pad.guid);
                choices.push({ text: pad.name, value: pad.guid, name: pad.name });
            }
        }
        for (const stored of options.controllers) {
            if (!known.has(stored.guid)) {
                known.add(stored.guid);
                choices.push({ text: qsTr("%1 (not connected)").arg(stored.name), value: stored.guid, name: stored.name });
            }
        }
        return choices;
    }

    function startCapture(action) {
        message = "";
        captureAction = action;
        game.gamepad.captureButton();
    }

    function cancelCapture() {
        captureAction = "";
        // Also while the page is made (no game yet).
        game?.gamepad.cancelCapture();
    }

    title: qsTr("Controller")
    // The pad's Y does what "Reset buttons" does.
    resettable: options.editable
    resetButton: resetButtonsButton
    onResetRequested: {
        cancelCapture();
        options.resetBindings();
        message = qsTr("The buttons are back to their defaults.");
    }

    onVisibleChanged: {
        if (visible && game)
            options.editProfileOf(game.gamepad.pads.guid);
        else
            cancelCapture();
    }

    Connections {
        target: page.game.gamepad

        function onButtonCaptured(button) {
            if (page.captureAction.length > 0)
                page.message = page.options.bind(page.captureAction, button);
            page.captureAction = "";
        }

        function onCapturingChanged() {
            if (!page.game.gamepad.capturing)
                page.captureAction = "";
        }
    }

    SettingGroup {
        title: qsTr("Profile")

        SettingDropdown {
            id: profileBox

            objectName: "gamepadProfile"
            label: qsTr("Settings for")
            help: page.options.profileKey.length === 0 ? ""
                  : (page.options.ownProfile ? qsTr("This controller model has its own settings.")
                                             : qsTr("This controller uses the settings for all controllers."))
                    + " " + qsTr("Over USB and over Bluetooth a controller counts as two models.")
            textRole: "text"
            valueRole: "value"
            model: page.profileChoices
            currentIndex: Math.max(0, indexOfValue(page.options.profileKey))
            onActivated: page.options.profileKey = currentValue
        }

        SettingRow {
            label: qsTr("Own settings")
            visible: page.options.profileKey.length > 0

            GameButton {
                objectName: "ownProfileButton"
                text: page.options.ownProfile ? qsTr("Use the shared settings") : qsTr("Own settings")
                onClicked: {
                    if (page.options.ownProfile) {
                        page.options.removeOwnProfile();
                    } else {
                        const choice = page.profileChoices[profileBox.currentIndex];
                        page.options.makeOwnProfile(choice.value, choice.name);
                    }
                }
            }
            Item {
                Layout.fillWidth: true
            }
        }

        Hint {
            Layout.fillWidth: true
            Layout.bottomMargin: Ui.space.xs
            text: page.game.gamepad.pads.list.length === 0
                  ? qsTr("No controller connected. Xbox, PlayStation and Switch Pro controllers and most others work over USB or Bluetooth.")
                  : qsTr("Connected: %1. The controller you press a button on is used.")
                        .arg(page.game.gamepad.pads.list.map(pad => pad.inUse ? qsTr("%1 (in use)").arg(pad.name) : pad.name).join(", "))
        }
    }

    SettingGroup {
        title: qsTr("Sticks and triggers")

        GamepadValues {
            options: page.options
        }
    }

    SettingGroup {
        title: qsTr("Buttons")

        SettingSwitch {
            objectName: "radialOnly"
            label: qsTr("Radial menu only")
            help: qsTr("The hotbar shows just the slot in the hand")
            enabled: page.options.editable
            checked: page.options.radialOnly
            onToggled: page.options.radialOnly = checked
        }

        Repeater {
            model: page.options.rows

            delegate: GamepadBindingRow {
                game: page.game
                capturing: page.captureAction === modelData.action
                changeEnabled: page.options.editable && page.game.gamepad.pads.list.length > 0
                onChangeClicked: page.startCapture(modelData.action)
            }
        }

        Hint {
            Layout.fillWidth: true
            visible: page.message.length > 0
            kind: Hint.Warning
            text: page.message
        }

        Hint {
            Layout.fillWidth: true
            text: page.game.gamepad.pads.list.length === 0 ? qsTr("Connect a controller to change its buttons.")
                  : qsTr("The triggers mine and use; in menus the buttons keep their places (bottom confirms, right goes back).")
        }

        RowLayout {
            Layout.topMargin: Ui.space.xs
            Layout.bottomMargin: Ui.space.xs
            spacing: Ui.space.s

            GameButton {
                id: resetButtonsButton

                objectName: "resetButtonsButton"
                text: qsTr("Reset buttons")
                enabled: page.options.editable
                onClicked: page.resetRequested(false)
            }
            GameButton {
                text: qsTr("Reset everything")
                enabled: page.options.editable
                onClicked: {
                    page.cancelCapture();
                    page.options.resetProfile();
                    page.message = qsTr("All values and buttons are back to their defaults.");
                }
            }
        }
    }
}
