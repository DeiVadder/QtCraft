import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: root

    width: 1280
    height: 720
    visible: true
    title: qsTr("QtCraft")

    WorldView {
        id: worldView

        objectName: "worldView"
        anchors.fill: parent
    }

    Label {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        color: "white"
        font.pixelSize: 18
        text: qsTr("QtCraft renderer spike")
    }
}
