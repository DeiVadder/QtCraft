import QtQuick
import QtQuick.Controls
import QtCraft

Item {
    id: overlay

    required property GameController game

    onVisibleChanged: {
        if (visible) {
            input.text = overlay.game.chatStart;
            input.forceActiveFocus();
            // Opened with the pad: the line is typed on the on-screen keyboard (decision E-584).
            overlay.game.gamepad.requestKeyboard(input);
        }
    }
    // Keys the line leaves (function keys, ...) end here.
    Keys.onPressed: event => event.accepted = true

    TextField {
        id: input

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        placeholderText: overlay.game.gamepad.active ? qsTr("Chat or /command (e.g. /help), Start on the keyboard sends, B cancels")
                                                   : qsTr("Chat or /command (e.g. /help), Enter to send, Esc to cancel")
        Keys.onEscapePressed: overlay.game.overlay = ""
        onAccepted: {
            overlay.game.sendChat(text);
            overlay.game.overlay = "";
        }
    }
}
