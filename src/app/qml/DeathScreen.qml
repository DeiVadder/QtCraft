import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

Rectangle {
    id: screen

    required property GameController game

    color: "#a0600000"

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 16

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("You died")
            color: "white"
            font.pixelSize: 48
            font.bold: true
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Your inventory is kept.")
            color: "#e0ffffff"
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Respawn")
            highlighted: true
            onClicked: screen.game.respawn()
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Quit to menu")
            onClicked: screen.game.leaveGame()
        }
    }
}
