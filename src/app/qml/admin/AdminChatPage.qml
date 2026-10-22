import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The chat of "Manage server" (decision E-863): the recent lines of all players and of the server,
// and a line to write to everyone as the server (/say).
ColumnLayout {
    id: page

    required property AdminController admin

    function send() {
        page.admin.sendChat(chatField.text);
        chatField.text = "";
    }

    spacing: Ui.space.m

    Label {
        text: qsTr("Chat")
        font.pixelSize: Ui.type.section
        font.bold: true
        color: Theme.text
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    AdminLineList {
        title: qsTr("Chat lines")
        lines: page.admin.chat
        emptyText: qsTr("No chat yet. Lines of the players and of the server show here.")
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Ui.space.s

        GameTextField {
            id: chatField

            objectName: "adminChatField"
            Layout.fillWidth: true
            maximumLength: page.admin.textLength
            placeholderText: qsTr("Message to everyone, as the server")
            Accessible.name: qsTr("Message to everyone")
            onAccepted: page.send()
        }
        GameButton {
            objectName: "adminChatSend"
            kind: GameButton.Primary
            text: qsTr("Send")
            enabled: chatField.text.trim().length > 0
            onClicked: page.send()
        }
    }
}
