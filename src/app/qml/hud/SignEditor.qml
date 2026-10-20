pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The editor of a sign (decision E-766), opened by the server when the player places or uses one:
// a wooden board with one field per line (at most the sign's line length). Enter goes to the next
// line, on the last one it is done like the Done button and Esc; the server keeps the cleaned lines
// and shows them to everyone. With the pad each line is typed on the on-screen keyboard, whose
// start key goes on to the next line.
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
    // Keys the fields leave (function keys, ...) end here.
    Keys.onPressed: event => event.accepted = true

    Rectangle {
        anchors.fill: parent
        color: Theme.scrim
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - Ui.px(64), Ui.px(480))
        spacing: Ui.px(12)

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Edit sign")
            font.pixelSize: Ui.px(24)
            font.bold: true
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: lineColumn.implicitHeight + 2 * Ui.px(16)
            radius: Ui.px(4)
            color: Theme.signBoard
            border.color: Theme.signBorder
            border.width: Ui.px(4)

            ColumnLayout {
                id: lineColumn

                anchors.fill: parent
                anchors.margins: Ui.px(16)
                spacing: Ui.px(6)

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
                        onAccepted: editor.focusLine(index + 1)
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.textSecondary
            text: editor.game.gamepad.active ? qsTr("%1 on the keyboard goes to the next line").arg(editor.game.gamepad.pads.texts.start)
                                             : qsTr("Enter goes to the next line, Esc is done")
        }

        Button {
            objectName: "signDone"
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Done")
            highlighted: true
            onClicked: editor.done()
        }
    }
}
