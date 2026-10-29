pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The editor of a sign (decision E-766), opened by the server when the player places or uses one:
// a screen over the world (ScreenFrame, head with "Close", docs/UI-Leitfaden.md 5) with a wooden
// board of one field per line (at most the sign's line length). The text holds at once (pattern 1):
// each line goes to the sign's draft as it is typed (GameController.sign.edit), and closing in any
// way sends it; there is no "Done". Enter goes to the next line, on the last one, Esc and "Close"
// close the editor; the server keeps the cleaned lines and shows them to everyone. With the pad each
// line is typed on the on-screen keyboard, whose start key goes on to the next line.
Item {
    id: editor

    required property GameController game
    readonly property var sign: game.sign

    function done() {
        const lines = [];
        for (let index = 0; index < lineFields.count; ++index)
            lines.push((lineFields.itemAt(index) as TextField).text);
        editor.sign.write(lines);
    }

    function focusLine(index: int) {
        const field = lineFields.itemAt(index);
        if (!field) {
            editor.done();
            return;
        }
        field.forceActiveFocus();
        editor.game.gamepad.requestKeyboard(field);
    }

    objectName: "signEditor"

    onVisibleChanged: {
        if (visible)
            Qt.callLater(() => editor.focusLine(0));
    }
    // Keys the fields leave (function keys, ...) end here; Esc closes as on the fields.
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape)
            editor.done();
        event.accepted = true;
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.scrim
    }

    ScreenFrame {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Ui.screen.margin, Ui.screen.s)
        height: content.implicitHeight + 2 * Ui.space.xl

        ColumnLayout {
            id: content

            anchors.fill: parent
            anchors.margins: Ui.space.xl
            spacing: Ui.space.l

            ScreenHeader {
                Layout.fillWidth: true
                title: qsTr("Edit sign")
                closeName: "signClose"
                padKey: editor.game.gamepad.active ? editor.game.gamepad.pads.texts.east : ""
                onCloseRequested: editor.done()
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: lineColumn.implicitHeight + 2 * Ui.space.l
                radius: Ui.radius
                color: Theme.signBoard
                border.color: Theme.signBorder
                border.width: Ui.size.signBorder

                ColumnLayout {
                    id: lineColumn

                    anchors.fill: parent
                    anchors.margins: Ui.space.l
                    spacing: Ui.space.xs

                    Repeater {
                        id: lineFields

                        model: editor.visible ? editor.sign.lineCount : 0

                        TextField {
                            required property int index

                            objectName: "signLine" + index
                            Accessible.name: qsTr("Line %1 of the sign").arg(index + 1)
                            Layout.fillWidth: true
                            horizontalAlignment: TextInput.AlignHCenter
                            text: editor.sign.lines[index] ?? ""
                            maximumLength: editor.sign.lineLength
                            selectByMouse: true
                            Keys.onEscapePressed: editor.done()
                            // textChanged, not textEdited: the pad's on-screen keyboard types with
                            // insert() and remove(), which do not emit textEdited. sign.lines is the
                            // draft, read afresh here: the field filled from it changes nothing.
                            onTextChanged: {
                                if (editor.visible && text !== (editor.sign.lines[index] ?? ""))
                                    editor.sign.edit(index, text);
                            }
                            onAccepted: editor.focusLine(index + 1)
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textMuted
                font.pixelSize: Ui.type.help
                text: editor.game.gamepad.active
                      ? qsTr("The text holds at once; %1 on the keyboard goes to the next line.").arg(editor.game.gamepad.pads.texts.start)
                      : qsTr("The text holds at once; Enter goes to the next line.")
            }
        }
    }
}
