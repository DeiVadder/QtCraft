pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The gamepad page of the settings (decisions E-610, E-611, E-613, E-614): which profile is edited
// (the one all controllers use, or a controller model's own), look speed, invert, dead zones, the
// response curve, the trigger point, vibration, the radial menu alone, and the buttons of the
// actions. "Change" waits for the next button on any controller (a button used by another action is
// swapped); a key or a click anywhere (GameController.gamepad takes them), the last controller
// disconnecting or leaving the page cancels. The controllers connected now are listed, the one in
// use marked: pressing a button on another one hands it over. The page opens on the profile of the
// controller in use when its model has one of its own.
Flickable {
    id: page

    required property GameController game
    readonly property GamepadOptions options: game.settings.gamepad
    // The action waiting for a button ("" while none does) and what the last change moved.
    property string captureAction
    property string message

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

    implicitHeight: content.implicitHeight
    clip: true
    contentWidth: width
    contentHeight: content.implicitHeight
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ThemedScrollBar {}

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

    ColumnLayout {
        id: content

        // A gutter on the right for the scroll bar.
        width: page.width - 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Label {
                text: qsTr("Settings for")
            }
            ComboBox {
                id: profileBox

                objectName: "gamepadProfile"
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: page.profileChoices
                currentIndex: Math.max(0, indexOfValue(page.options.profileKey))
                onActivated: page.options.profileKey = currentValue
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: page.options.profileKey.length > 0
            spacing: 12

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.7
                text: (page.options.ownProfile ? qsTr("This controller model has its own settings.")
                                               : qsTr("This controller uses the settings for all controllers."))
                      + " " + qsTr("Over USB and over Bluetooth a controller counts as two models.")
            }
            Button {
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
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            opacity: 0.7
            text: page.game.gamepad.pads.list.length === 0
                  ? qsTr("No controller connected. Xbox, PlayStation and Switch Pro controllers and most others work over USB or Bluetooth.")
                  : qsTr("Connected: %1. The controller you press a button on is used.")
                        .arg(page.game.gamepad.pads.list.map(pad => pad.inUse ? qsTr("%1 (in use)").arg(pad.name) : pad.name).join(", "))
        }

        GamepadValues {
            Layout.fillWidth: true
            options: page.options
        }

        CheckBox {
            objectName: "radialOnly"
            text: qsTr("Radial menu only: the hotbar shows just the slot in the hand")
            checked: page.options.radialOnly
            onToggled: page.options.radialOnly = checked
        }

        Label {
            text: qsTr("Buttons")
            font.bold: true
        }

        Repeater {
            model: page.options.rows

            delegate: RowLayout {
                id: bindingRow

                required property var modelData
                readonly property bool capturing: page.captureAction === modelData.action

                Layout.fillWidth: true
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    text: bindingRow.modelData.label
                    elide: Text.ElideRight
                }
                PadGlyph {
                    visible: bindingRow.modelData.button.length > 0
                    symbol: page.game.gamepad.pads.glyph(page.game.gamepad.pads.family, bindingRow.modelData.button)
                }
                // The fixed combination (modifier held plus a button), read-only (decision E-692).
                PadGlyph {
                    visible: bindingRow.modelData.comboModifier !== undefined
                    symbol: page.game.gamepad.pads.glyph(page.game.gamepad.pads.family, bindingRow.modelData.comboModifier ?? "")
                }
                Label {
                    visible: bindingRow.modelData.comboModifier !== undefined
                    text: "+"
                }
                PadGlyph {
                    visible: bindingRow.modelData.comboButton !== undefined
                    symbol: page.game.gamepad.pads.glyph(page.game.gamepad.pads.family, bindingRow.modelData.comboButton ?? "")
                }
                Label {
                    visible: bindingRow.modelData.button.length === 0 && bindingRow.modelData.comboModifier === undefined
                    text: qsTr("none")
                    opacity: 0.7
                }
                Button {
                    Layout.preferredWidth: 170
                    enabled: page.options.editable && page.game.gamepad.pads.list.length > 0
                    highlighted: bindingRow.capturing
                    text: bindingRow.capturing ? qsTr("Press a button …") : qsTr("Change")
                    onClicked: page.startCapture(bindingRow.modelData.action)
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: page.message
            wrapMode: Text.Wrap
            color: Theme.warning
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            opacity: 0.7
            text: qsTr("The triggers mine and use; in menus the buttons keep their places (bottom confirms, right goes back).")
        }

        RowLayout {
            spacing: 12

            Button {
                text: qsTr("Reset buttons")
                enabled: page.options.editable
                onClicked: {
                    page.cancelCapture();
                    page.options.resetBindings();
                    page.message = qsTr("The buttons are back to their defaults.");
                }
            }
            Button {
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
