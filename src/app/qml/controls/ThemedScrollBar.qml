import QtQuick
import QtQuick.Controls
import QtCraft

// The scroll bar of every scrolling screen: a handle and a track from the palette, shown for as
// long as the content does not fit, so a part that scrolls says so at a glance (the style's bar
// on macOS hides until the content moves). The mouse wheel scrolls the view it belongs to.
ScrollBar {
    id: bar

    policy: bar.size < 1.0 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    minimumSize: 0.1
    padding: 2

    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: 3
        color: bar.pressed ? Theme.text : bar.hovered ? Theme.textSecondary : Theme.textMuted
    }

    background: Rectangle {
        implicitWidth: 10
        implicitHeight: 10
        radius: 5
        color: Theme.scrim
    }
}
