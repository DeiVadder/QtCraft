import QtQuick
import QtQuick.Controls
import QtCraft

// A short notice at the top after the screenshot key saved a picture or failed to (decision E-692),
// shown for shownMs; show() starts it again. It is hidden while the picture is taken.
Rectangle {
    id: notice

    readonly property int shownMs: 2500
    // Read by the column of notices in Main.qml (the effective `visible` would loop through it).
    readonly property bool showing: timer.running
    property string message

    // Shows `text` for shownMs.
    function show(text: string) {
        message = text;
        timer.restart();
    }

    width: label.implicitWidth + 28
    height: label.implicitHeight + 14
    radius: 4
    color: Theme.hudBackdrop
    visible: timer.running

    Label {
        id: label

        objectName: "photoNotice"
        anchors.centerIn: parent
        text: notice.message
        color: Theme.text
        font.pixelSize: 15
    }

    Timer {
        id: timer

        interval: notice.shownMs
    }
}
