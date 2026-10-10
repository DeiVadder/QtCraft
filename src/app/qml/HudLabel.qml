import QtQuick
import QtQuick.Controls
import QtCraft

// A text over the world on a dark, semi-transparent backdrop, so it stays readable over snow,
// sand, water and the night sky (contrast checked in test_ui_palette).
Label {
    color: Theme.text
    leftPadding: 6
    rightPadding: 6
    topPadding: 2
    bottomPadding: 2
    font.pixelSize: 14

    background: Rectangle {
        color: Theme.hudBackdrop
        radius: 3
    }
}
