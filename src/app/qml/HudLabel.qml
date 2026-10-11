import QtQuick
import QtQuick.Controls
import QtCraft

// A text over the world on a dark, semi-transparent backdrop, so it stays readable over snow,
// sand, water and the night sky (contrast checked in test_ui_palette). Plain text: chat lines and
// names come from other players and the server and must never load images or links.
Label {
    textFormat: Text.PlainText
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
