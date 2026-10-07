import QtQuick
import QtQuick.Controls
import QtCraft

Item {
    id: overlay

    required property GameController game

    onVisibleChanged: {
        if (visible) {
            input.text = "";
            input.forceActiveFocus();
        }
    }

    TextField {
        id: input

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        placeholderText: qsTr("Chat or /command (e.g. /help), Enter to send, Esc to cancel")
        Keys.onEscapePressed: overlay.game.overlay = ""
        onAccepted: {
            overlay.game.sendChat(text);
            overlay.game.overlay = "";
        }
    }
}
