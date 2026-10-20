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
    padding: Ui.px(2)

    contentItem: Rectangle {
        implicitWidth: Ui.px(6)
        implicitHeight: Ui.px(6)
        radius: Ui.px(3)
        color: bar.pressed ? Theme.text : bar.hovered ? Theme.textSecondary : Theme.textMuted
    }

    background: Rectangle {
        implicitWidth: Ui.px(10)
        implicitHeight: Ui.px(10)
        radius: Ui.px(5)
        color: Theme.scrim
    }
}
