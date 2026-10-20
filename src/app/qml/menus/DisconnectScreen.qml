import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The game ended without the player leaving (decision E-261): the server stopped, kicked or
// refused the player, or the connection broke. Shows why; back to the menu or, in a network game,
// join the same server again.
Rectangle {
    id: screen

    required property GameController game

    color: Theme.scrimStrong
    focus: visible
    onVisibleChanged: if (visible) forceActiveFocus()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape)
            screen.game.leaveGame();
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - Ui.px(32), Ui.px(560))
        spacing: Ui.px(16)

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: screen.game.multiplayer.networkGame ? qsTr("Disconnected from the server") : qsTr("The game ended")
            color: Theme.text
            font.pixelSize: Ui.px(30)
            font.bold: true
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.PlainText
            text: screen.game.statusText
            color: Theme.textSecondary
            font.pixelSize: Ui.px(18)
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Ui.px(12)

            Button {
                visible: screen.game.multiplayer.networkGame && screen.game.multiplayer.lastAddress.length > 0
                text: qsTr("Join again")
                onClicked: {
                    screen.game.leaveGame();
                    screen.game.multiplayer.joinAgain();
                }
            }

            Button {
                text: qsTr("Back to menu")
                highlighted: true
                onClicked: screen.game.leaveGame()
            }
        }
    }
}
