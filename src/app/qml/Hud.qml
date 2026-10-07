import QtQuick
import QtQuick.Controls

Item {
    id: hud

    property string debugText
    property bool mouseCaptured

    Label {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        color: "white"
        style: Text.Outline
        styleColor: "#80000000"
        font.pixelSize: 14
        text: hud.debugText
    }

    Item {
        id: crosshair

        anchors.centerIn: parent
        width: 18
        height: 18

        Rectangle {
            anchors.centerIn: parent
            width: parent.width
            height: 2
            color: "#e0ffffff"
        }

        Rectangle {
            anchors.centerIn: parent
            width: 2
            height: parent.height
            color: "#e0ffffff"
        }
    }

    Label {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        visible: !hud.mouseCaptured
        color: "white"
        style: Text.Outline
        styleColor: "#a0000000"
        font.pixelSize: 16
        text: qsTr("Click to look around. WASD to move, Space to jump, Shift to sneak, Esc to release the mouse.")
    }
}
