pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The on-screen keyboard for text fields while the pad leads (decision E-584): opened for a field
// (GameController.gamepad.keyboardRequested), it types into that field and shows its text above
// the keys (as the field shows it: a password field's dots). Every key has a second character under
// shift (digits their symbols, letters their capitals); the keys cover addresses with a port and
// IPv6 ("[::1]:25565"), umlauts and ß. The pad moves over the keys like over any screen; besides,
// west deletes, north types a space, the shoulders move the cursor and start finishes (the field's
// accepted(), e.g. the chat line sends). Back closes it and keeps what was typed.
Popup {
    id: keyboard

    // The text field typed into (a TextField, TextInput or TextEdit); null once it is destroyed.
    property Item target
    // The same field untyped for its text methods, which TextInput and TextEdit share without a type.
    readonly property var editor: target
    property bool shifted
    // The key the focus starts on.
    property Item firstKey
    // The window's size, where the keyboard sits at the bottom.
    required property real areaWidth
    required property real areaHeight
    // The pad's buttons in running text (GameController.gamepad.pads.texts, decision E-612).
    property var buttonTexts: ({ west: "X", north: "Y", leftShoulder: "LB", rightShoulder: "RB", start: "Menu", east: "B" })

    // Each key: its character and the one under shift.
    readonly property var rows: [
        ["1!", "2@", "3#", "4$", "5%", "6^", "7&", "8*", "9(", "0)", "-_", "=+"],
        ["qQ", "wW", "eE", "rR", "tT", "yY", "uU", "iI", "oO", "pP", "[{", "]}"],
        ["aA", "sS", "dD", "fF", "gG", "hH", "jJ", "kK", "lL", ":;", "@'", "äÄ"],
        ["zZ", "xX", "cC", "vV", "bB", "nN", "mM", ",<", ".:", "/?", "öÖ", "üÜ"],
        ["ßẞ", "#~", "\\|", "\"`", "<>", "!?"],
    ]

    function openFor(field: Item) {
        target = field;
        shifted = false;
        open();
    }

    function type(text) {
        if (!target)
            return;
        if (editor.selectedText.length > 0)
            editor.remove(editor.selectionStart, editor.selectionEnd);
        editor.insert(editor.cursorPosition, text);
        shifted = false;
    }

    function deleteBack() {
        if (!target)
            return;
        if (editor.selectedText.length > 0)
            editor.remove(editor.selectionStart, editor.selectionEnd);
        else if (editor.cursorPosition > 0)
            editor.remove(editor.cursorPosition - 1, editor.cursorPosition);
    }

    function moveCursor(step) {
        if (target)
            editor.cursorPosition = Math.max(0, Math.min(editor.length, editor.cursorPosition + step));
    }

    function finish() {
        const field = editor;
        close();
        if (field && field.accepted)
            field.accepted();
    }

    objectName: "onScreenKeyboard"
    x: Math.round((areaWidth - width) / 2)
    y: areaHeight - height - 24
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 14
    onOpened: {
        if (keyboard.firstKey)
            keyboard.firstKey.forceActiveFocus(Qt.TabFocusReason);
    }
    onClosed: {
        if (target)
            target.forceActiveFocus();
        target = null;
    }

    background: Rectangle {
        color: Theme.panel
        border.color: Theme.panelBorder
        radius: 6
    }

    contentItem: ColumnLayout {
        spacing: 6

        // Pad commands besides moving and pressing keys (QcMenuNavigator asks this first).
        function gamepadCommand(command: string): bool {
            switch (command) {
            case "secondary":
                keyboard.deleteBack();
                return true;
            case "tertiary":
                keyboard.type(" ");
                return true;
            case "tabPrevious":
                keyboard.moveCursor(-1);
                return true;
            case "tabNext":
                keyboard.moveCursor(1);
                return true;
            case "start":
                keyboard.finish();
                return true;
            case "back":
                keyboard.close();
                return true;
            }
            return false;
        }

        Label {
            objectName: "keyboardPreview"
            Layout.fillWidth: true
            Layout.preferredWidth: 560
            // displayText: what the field shows (a password's dots); a TextEdit has only its text.
            text: keyboard.target ? (keyboard.editor.displayText ?? keyboard.editor.text) : ""
            color: Theme.text
            elide: Text.ElideLeft
            font.pixelSize: 18
            padding: 6

            background: Rectangle {
                color: Theme.slot
                border.color: Theme.slotBorder
            }
        }

        Repeater {
            model: keyboard.rows

            RowLayout {
                id: keyRow

                required property var modelData
                required property int index

                Layout.alignment: Qt.AlignHCenter
                spacing: 4

                Repeater {
                    model: keyRow.modelData

                    Button {
                        id: keyButton

                        required property string modelData
                        required property int index
                        readonly property string character: keyboard.shifted ? modelData.charAt(1) : modelData.charAt(0)

                        objectName: "key_" + modelData.charAt(0)
                        Layout.preferredWidth: 46
                        text: character
                        onClicked: keyboard.type(character)
                        Component.onCompleted: {
                            if (keyRow.index === 1 && index === 0)
                                keyboard.firstKey = keyButton;
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 4

            Button {
                objectName: "keyShift"
                text: qsTr("Shift")
                checkable: true
                checked: keyboard.shifted
                onClicked: keyboard.shifted = !keyboard.shifted
            }

            Button {
                objectName: "keySpace"
                Layout.preferredWidth: 200
                text: qsTr("Space")
                onClicked: keyboard.type(" ")
            }

            Button {
                objectName: "keyBackspace"
                text: qsTr("Delete")
                onClicked: keyboard.deleteBack()
            }

            Button {
                objectName: "keyDone"
                text: qsTr("Done")
                highlighted: true
                onClicked: keyboard.finish()
            }
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("%1: delete  %2: space  %3/%4: cursor  %5: done  %6: close").arg(keyboard.buttonTexts.west).arg(keyboard.buttonTexts.north)
                      .arg(keyboard.buttonTexts.leftShoulder).arg(keyboard.buttonTexts.rightShoulder).arg(keyboard.buttonTexts.start).arg(keyboard.buttonTexts.east)
            color: Theme.textMuted
            font.pixelSize: 12
        }
    }
}
