// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The controls page of the settings: the mouse (sensitivity, invert), then the key table, every
// action with its primary and secondary key. Clicking a key waits for the next key press; Esc
// cancels, Backspace removes the key, a key used elsewhere is swapped (the message above the table,
// scrolled into view, says what moved). KeyControls does the capture; the page shows it.
SettingsPage {
    id: page

    required property GameController game
    readonly property KeyControls keys: game.settings.keys

    // Follows what a key did to the capture (KeyControls): the focus leaves the capture when it
    // ended, and after a binding the page keeps its place (the table is rebuilt from the new
    // bindings) and shows the message.
    function follow(result: int, position: real) {
        if (result === KeyControls.Bound || result === KeyControls.Cancelled)
            page.forceActiveFocus();
        if (result === KeyControls.Bound) {
            contentY = position;
            Qt.callLater(revealMessage);
        }
    }

    // Scrolls the page so the message above the key table is in view (once laid out).
    function revealMessage() {
        if (message.length > 0)
            reveal(messageHint);
    }

    function startCapture(action, slot) {
        keys.startCapture(action, slot);
        if (keys.capturing)
            keyCapture.forceActiveFocus();
    }

    function cancelCapture() {
        if (!keys.capturing)
            return;

        keys.cancelCapture();
        page.forceActiveFocus();
    }

    function clear() {
        cancelCapture();
        keys.clearCapture();
    }

    title: qsTr("Controls")
    message: keys.message
    resettable: true
    resetButton: resetKeysButton
    // From the pad the focus stays on the reset button and the screen shows the message.
    onResetRequested: byPad => {
        cancelCapture();
        keys.reset();
        if (!byPad)
            Qt.callLater(revealMessage);
    }

    SettingGroup {
        title: qsTr("Mouse")

        SettingSlider {
            label: qsTr("Mouse sensitivity")
            from: 0.02
            to: 1.0
            stepSize: 0.01
            value: page.game.settings.mouseSensitivity
            // In percent of the fastest setting (docs/UI-Leitfaden.md 6.4).
            valueText: qsTr("%1 %").arg(Math.round(value * 100))
            onMoved: page.game.settings.mouseSensitivity = value
        }

        SettingSwitch {
            label: qsTr("Invert mouse")
            checked: page.game.settings.invertMouse
            onToggled: page.game.settings.invertMouse = checked
        }
    }

    SettingGroup {
        title: qsTr("Keys")
        help: qsTr("Click a key and press the new one. Esc cancels, Backspace removes the key. A key used by another action is swapped.")

        // What the last change did ("… moved to …"): above the table, and scrolled into view.
        Hint {
            id: messageHint

            Layout.fillWidth: true
            visible: page.message.length > 0
            kind: Hint.Warning
            text: page.message
        }

        // The columns of the table: the action, its key and a second key (ES-7 of
        // docs/Menue-Pruefung.md).
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Ui.space.xs
            spacing: Ui.space.s

            Label {
                Layout.fillWidth: true
                text: qsTr("Action")
                font.pixelSize: Ui.type.help
                font.bold: true
                color: Theme.textSecondary
            }

            // Takes the next key while a slot waits for one. It takes the shortcuts too (Esc
            // would close the settings otherwise), so every key reaches it.
            Item {
                id: keyCapture

                // No room of its own beside the column heads.
                Layout.preferredWidth: 0

                // The fullscreen key leaves it alone while it waits (QcDisplaySettings).
                objectName: "keyCapture"
                Keys.onShortcutOverride: event => event.accepted = true
                Keys.onPressed: event => {
                    event.accepted = true;
                    const position = page.contentY;
                    page.follow(page.keys.keyPressed(event.key, event.isAutoRepeat), position);
                }
                Keys.onReleased: event => {
                    event.accepted = true;
                    const position = page.contentY;
                    page.follow(page.keys.keyReleased(event.key, event.isAutoRepeat), position);
                }
            }
            Label {
                Layout.preferredWidth: Ui.size.keyButton
                text: qsTr("Key")
                font.pixelSize: Ui.type.help
                font.bold: true
                color: Theme.textSecondary
                horizontalAlignment: Text.AlignHCenter
            }
            Label {
                Layout.preferredWidth: Ui.size.keyButton
                text: qsTr("Second key")
                font.pixelSize: Ui.type.help
                font.bold: true
                color: Theme.textSecondary
                horizontalAlignment: Text.AlignHCenter
            }
        }

        Repeater {
            model: page.keys.rows

            delegate: RowLayout {
                id: keyRow

                required property var modelData

                Layout.fillWidth: true
                spacing: Ui.space.s

                Label {
                    Layout.fillWidth: true
                    text: keyRow.modelData.label
                    font.pixelSize: Ui.type.body
                    color: Theme.text
                    elide: Text.ElideRight
                }

                Repeater {
                    model: 2

                    delegate: GameButton {
                        required property int index
                        readonly property bool capturing: page.keys.captureAction === keyRow.modelData.action && page.keys.captureSlot === index

                        Layout.preferredWidth: Ui.size.keyButton
                        enabled: !keyRow.modelData.fixed
                        kind: capturing ? GameButton.Primary : GameButton.Secondary
                        text: capturing ? qsTr("Press a key …") : (index === 0 ? keyRow.modelData.primary : keyRow.modelData.secondary)
                        // "–" or an arrow says little without the row: the action, which key and the key.
                        Accessible.name: qsTr("%1, %2: %3").arg(keyRow.modelData.label)
                                         .arg(index === 0 ? qsTr("primary key") : qsTr("secondary key"))
                                         .arg(text === "–" ? qsTr("none") : text)
                        onClicked: page.startCapture(keyRow.modelData.action, index)
                    }
                }
            }
        }

        Hint {
            Layout.fillWidth: true
            text: qsTr("On a Mac, F1–F12 reach the game only while fn is held or with “Use F1, F2, etc. keys as standard function keys” switched on in the system settings (Keyboard).")
        }

        GameButton {
            id: resetKeysButton

            objectName: "resetKeysButton"
            Layout.topMargin: Ui.space.xs
            Layout.bottomMargin: Ui.space.xs
            text: qsTr("Reset to defaults")
            onClicked: page.resetRequested(false)
        }
    }
}
