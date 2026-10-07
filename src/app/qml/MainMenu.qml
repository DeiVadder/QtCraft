import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: menu

    signal playRequested(string worldName, string seed)
    signal quitRequested

    gradient: Gradient {
        GradientStop { position: 0.0; color: "#5f9bd1" }
        GradientStop { position: 1.0; color: "#2f4f6e" }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 420)
        spacing: 12

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("QtCraft")
            color: "white"
            font.pixelSize: 64
            font.bold: true
        }

        Label {
            text: qsTr("World name")
            color: "white"
        }

        TextField {
            id: worldName

            Layout.fillWidth: true
            text: qsTr("New World")
            selectByMouse: true
        }

        Label {
            text: qsTr("Seed (number or text, empty for random)")
            color: "white"
        }

        TextField {
            id: seed

            Layout.fillWidth: true
            selectByMouse: true
        }

        Button {
            Layout.fillWidth: true
            text: qsTr("Play")
            highlighted: true
            onClicked: menu.playRequested(worldName.text,
                                          seed.text.length > 0 ? seed.text : String(Math.floor(Math.random() * 2147483647)))
        }

        Button {
            Layout.fillWidth: true
            text: qsTr("Quit")
            onClicked: menu.quitRequested()
        }
    }
}
