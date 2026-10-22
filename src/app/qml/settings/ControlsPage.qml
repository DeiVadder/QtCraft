pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The key table of the settings: every action with its primary and secondary key. Clicking a key
// waits for the next key press; Esc cancels, Backspace removes the key, a key used elsewhere is
// swapped (the message says what moved).
ColumnLayout {
    id: page

    required property KeyControls keys
    // The slot waiting for a key press: action id and slot (0 primary, 1 secondary).
    property string captureAction
    property int captureSlot: -1
    property string message
    // A modifier pressed while capturing; it is bound when released without another key, so
    // Shift+7 binds the character it types ("/" on a German keyboard).
    property int pendingModifier: 0

    function isModifier(key) {
        return key === Qt.Key_Shift || key === Qt.Key_Control || key === Qt.Key_Meta || key === Qt.Key_Alt
               || key === Qt.Key_AltGr;
    }

    function bindCaptured(key) {
        // The list is rebuilt from the new table; keep its position.
        const contentY = keyList.contentY;
        message = keys.bind(captureAction, captureSlot, key);
        cancelCapture();
        keyList.contentY = contentY;
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
        keyList.forceActiveFocus();
    }

    function clear() {
        cancelCapture();
        message = "";
    }

    spacing: Ui.px(8)

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

    Label {
        Layout.fillWidth: true
        text: qsTr("Click a key and press the new one. Esc cancels, Backspace removes the key. A key used by another action is swapped.")
        wrapMode: Text.Wrap
        opacity: 0.7
    }

    ListView {
        id: keyList

        Accessible.name: qsTr("Keys")
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: Ui.px(160)
        clip: true
        spacing: Ui.px(2)
        model: page.keys.rows
        ScrollBar.vertical: ThemedScrollBar {}

        delegate: RowLayout {
            id: keyRow

            required property var modelData

            width: ListView.view.width - Ui.px(12)
            spacing: Ui.px(8)

            Label {
                Layout.fillWidth: true
                text: keyRow.modelData.label
                elide: Text.ElideRight
            }

            Repeater {
                model: 2

                delegate: Button {
                    required property int index
                    readonly property bool capturing: page.captureAction === keyRow.modelData.action && page.captureSlot === index

                    Layout.preferredWidth: Ui.px(130)
                    enabled: !keyRow.modelData.fixed
                    highlighted: capturing
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

    Label {
        Layout.fillWidth: true
        visible: text.length > 0
        text: page.message
        wrapMode: Text.Wrap
        color: Theme.warning
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("On a Mac, F1–F12 reach the game only while fn is held or with “Use F1, F2, etc. keys as standard function keys” switched on in the system settings (Keyboard).")
        wrapMode: Text.Wrap
        opacity: 0.7
    }

    Button {
        text: qsTr("Reset to defaults")
        onClicked: {
            page.cancelCapture();
            page.keys.reset();
            page.message = qsTr("All keys are back to their defaults.");
        }
    }
}
