import QtQuick
import QtQuick.Controls
import QtCraft

// A short notice at the top when a pad connects, disconnects or another pad takes over (decision
// E-613), shown for shownMs; a new notice starts it again.
Rectangle {
    id: notice

    required property GameController game
    readonly property int shownMs: 3000
    // Read by the column of notices in Main.qml (the effective `visible` would loop through it).
    readonly property bool showing: timer.running

    width: label.implicitWidth + 28
    height: label.implicitHeight + 14
    radius: 4
    color: Theme.hudBackdrop
    visible: timer.running

    Label {
        id: label

        objectName: "padNotice"
        anchors.centerIn: parent
        text: notice.game.gamepad.pads.notice
        color: Theme.text
        font.pixelSize: 15
    }

    Timer {
        id: timer

        interval: notice.shownMs
        running: true
    }

    Connections {
        target: notice.game.gamepad.pads

        function onNoticeChanged() {
            timer.restart();
        }
    }
}
