import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A reference to another handbook entry: icon, name and an optional note; a click opens the entry.
Rectangle {
    id: chip

    // {target, text, icon, note}
    required property var link

    signal activated(string target)

    implicitWidth: row.implicitWidth + 12
    implicitHeight: 28
    radius: 3
    color: hover.hovered ? "#60507aa8" : "#26ffffff"
    border.color: "#40ffffff"

    RowLayout {
        id: row

        x: 6
        anchors.verticalCenter: parent.verticalCenter
        spacing: 5

        Image {
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            visible: chip.link.icon.length > 0
            source: visible ? "image://items/" + chip.link.icon : ""
            sourceSize: Qt.size(20, 20)
            smooth: false
        }

        Label {
            text: chip.link.text
            color: "#a8d4ff"
        }

        Label {
            visible: chip.link.note.length > 0
            text: "(" + chip.link.note + ")"
            color: "#b8ffffff"
        }
    }

    HoverHandler {
        id: hover

        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: chip.activated(chip.link.target)
    }
}
