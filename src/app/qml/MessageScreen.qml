import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: screen

    property alias text: message.text
    property bool showBackButton

    signal backRequested

    color: "#c0202830"

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 520)
        spacing: 16

        Label {
            id: message

            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: "white"
            font.pixelSize: 22
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            visible: screen.showBackButton
            text: qsTr("Back to menu")
            onClicked: screen.backRequested()
        }
    }
}
