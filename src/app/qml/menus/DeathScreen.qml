import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

Rectangle {
    id: screen

    required property GameController game

    color: Theme.deathScrim
    // Takes the keyboard, so keys held when dying do nothing in the world below.
    focus: visible
    onVisibleChanged: if (visible) forceActiveFocus()
    Keys.onPressed: event => event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 16

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("You died")
            color: Theme.text
            font.pixelSize: 48
            font.bold: true
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Your inventory is kept.")
            color: Theme.textSecondary
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Respawn")
            highlighted: true
            onClicked: screen.game.player.respawn()
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Quit to menu")
            onClicked: screen.game.leaveGame()
        }
    }
}
