pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The controls page of the settings: the mouse (sensitivity, invert), then the key table, every
// action with its primary and secondary key. Clicking a key waits for the next key press; Esc
// cancels, Backspace removes the key, a key used elsewhere is swapped (the message above the table,
// scrolled into view, says what moved).
SettingsPage {
    id: page

    required property GameController game
    readonly property KeyControls keys: game.settings.keys
    // The slot waiting for a key press: action id and slot (0 primary, 1 secondary).
    property string captureAction
    property int captureSlot: -1
    // A modifier pressed while capturing; it is bound when released without another key, so
    // Shift+7 binds the character it types ("/" on a German keyboard).
    property int pendingModifier: 0

    function isModifier(key) {
        return key === Qt.Key_Shift || key === Qt.Key_Control || key === Qt.Key_Meta || key === Qt.Key_Alt
               || key === Qt.Key_AltGr;
    }

    function bindCaptured(key) {
        // The table is rebuilt from the new bindings; keep the page's position.
        const position = contentY;
        message = keys.bind(captureAction, captureSlot, key);
        cancelCapture();
        contentY = position;
        Qt.callLater(revealMessage);
    }

    // Scrolls the page so the message above the key table is in view (once laid out).
    function revealMessage() {
        if (message.length > 0)
            reveal(messageHint);
    }

    function startCapture(action, slot) {
        captureAction = action;
        captureSlot = slot;
        pendingModifier = 0;
        message = "";
        keyCapture.forceActiveFocus();
    }

    function cancelCapture() {
        if (captureSlot < 0)
            return;

        captureAction = "";
        captureSlot = -1;
        page.forceActiveFocus();
    }

    function clear() {
        cancelCapture();
        message = "";
    }

    title: qsTr("Controls")
    resettable: true
    resetButton: resetKeysButton
    // From the pad the focus stays on the reset button and the screen shows the message.
    onResetRequested: byPad => {
        cancelCapture();
        keys.reset();
        message = qsTr("All keys are back to their defaults.");
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

        Label {
            Layout.fillWidth: true
            Layout.topMargin: Ui.space.xs
            text: qsTr("Click a key and press the new one. Esc cancels, Backspace removes the key. A key used by another action is swapped.")
            font.pixelSize: Ui.type.help
            color: Theme.textMuted
            wrapMode: Text.Wrap

            // Takes the next key while a slot waits for one. It takes the shortcuts too (Esc would close the
            // settings otherwise), so every key reaches it.
            Item {
                id: keyCapture

                // The fullscreen key leaves it alone while it waits (QcDisplaySettings).
                objectName: "keyCapture"
                Keys.onShortcutOverride: event => event.accepted = true
                Keys.onPressed: event => {
                    event.accepted = true;
                    if (event.isAutoRepeat)
                        return;

                    if (event.key === Qt.Key_Escape) {
                        page.cancelCapture();
                    } else if (page.isModifier(event.key)) {
                        page.pendingModifier = event.key;
                    } else {
                        page.bindCaptured(event.key);
                    }
                }
                Keys.onReleased: event => {
                    event.accepted = true;
                    if (!event.isAutoRepeat && page.captureSlot >= 0 && event.key === page.pendingModifier)
                        page.bindCaptured(event.key);
                }
            }
        }

        // What the last change did ("… moved to …"): above the table, and scrolled into view.
        Hint {
            id: messageHint

            Layout.fillWidth: true
            visible: page.message.length > 0
            kind: Hint.Warning
            text: page.message
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
                        readonly property bool capturing: page.captureAction === keyRow.modelData.action && page.captureSlot === index

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
