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
// accepted(), e.g. the chat line sends). Back closes it and keeps what was typed. The letters follow
// the UI language's keyboard (QWERTZ in German, QWERTY otherwise); the pad's buttons below are
// drawn as on every screen (PadHintRow).
Popup {
    id: keyboard

    // Where y and z sit.
    enum KeyLayout {
        Qwerty,
        Qwertz
    }

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
    // The game for the pad's symbols below the keys; without one (a test) the hints are left out.
    property GameController game
    // The UI language ("en", "de"; GameSettings.contentLanguage, which follows a switch at runtime).
    property string language
    readonly property int keyLayout: language === "de" ? OnScreenKeyboard.KeyLayout.Qwertz : OnScreenKeyboard.KeyLayout.Qwerty

    // Each key: its character and the one under shift.
    readonly property var rows: {
        const qwertz = keyLayout === OnScreenKeyboard.KeyLayout.Qwertz;
        const upper = qwertz ? "zZ" : "yY";
        const lower = qwertz ? "yY" : "zZ";
        return [
            ["1!", "2@", "3#", "4$", "5%", "6^", "7&", "8*", "9(", "0)", "-_", "=+"],
            ["qQ", "wW", "eE", "rR", "tT", upper, "uU", "iI", "oO", "pP", "[{", "]}"],
            ["aA", "sS", "dD", "fF", "gG", "hH", "jJ", "kK", "lL", ":;", "@'", "äÄ"],
            [lower, "xX", "cC", "vV", "bB", "nN", "mM", ",<", ".:", "/?", "öÖ", "üÜ"],
            ["ßẞ", "#~", "\\|", "\"`", "<>", "!?"],
        ];
    }

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
    y: areaHeight - height - Ui.px(24)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: Ui.px(14)
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
        radius: Ui.px(6)
    }

    contentItem: ColumnLayout {
        spacing: Ui.px(6)

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
            Layout.preferredWidth: Ui.px(560)
            // displayText: what the field shows (a password's dots); a TextEdit has only its text.
            text: keyboard.target ? (keyboard.editor.displayText ?? keyboard.editor.text) : ""
            color: Theme.text
            elide: Text.ElideLeft
            font.pixelSize: Ui.px(18)
            padding: Ui.px(6)

            background: Rectangle {
                color: Theme.slot
                border.color: Theme.slotBorder
            }
        }

        // Counted, not the rows themselves: another layout relabels the keys in place.
        Repeater {
            model: keyboard.rows.length

            RowLayout {
                id: keyRow

                required property int index

                Layout.alignment: Qt.AlignHCenter
                spacing: Ui.px(4)

                Repeater {
                    model: keyboard.rows[keyRow.index].length

                    Button {
                        id: keyButton

                        required property int index
                        readonly property string pair: keyboard.rows[keyRow.index][index]
                        readonly property string character: keyboard.shifted ? pair.charAt(1) : pair.charAt(0)

                        objectName: "key_" + pair.charAt(0)
                        Layout.preferredWidth: Ui.px(46)
                        text: character
                        // A sign alone names nothing for a screen reader: "Key -".
                        Accessible.name: qsTr("Key %1").arg(character)
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
            spacing: Ui.px(4)

            Button {
                objectName: "keyShift"
                text: qsTr("Shift")
                checkable: true
                checked: keyboard.shifted
                onClicked: keyboard.shifted = !keyboard.shifted
            }

            Button {
                objectName: "keySpace"
                Layout.preferredWidth: Ui.px(200)
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

        Loader {
            Layout.alignment: Qt.AlignHCenter
            active: keyboard.game !== null

            sourceComponent: PadHintRow {
                game: keyboard.game
                fontSize: Ui.type.help
                entries: [
                    { input: "west", text: qsTr("delete") },
                    { input: "north", text: qsTr("space") },
                    { inputs: ["leftShoulder", "rightShoulder"], text: qsTr("cursor") },
                    { input: "start", text: qsTr("done") },
                    { input: "east", text: qsTr("close") }
                ]
            }
        }
    }
}
