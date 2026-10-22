import QtQuick
import QtQuick.Controls
import QtCraft

// A world's tile in the main menu (decision E-852): a raised block with the first letter of the
// world's name, standing in for a picture of the world (worlds keep none).
Rectangle {
    id: tile

    // The world's name (from its world.json).
    property string name

    implicitWidth: Ui.size.thumbnail
    implicitHeight: implicitWidth
    radius: Ui.radius
    color: Theme.panelRaised
    Accessible.ignored: true

    Bevel {}

    Label {
        anchors.centerIn: parent
        textFormat: Text.PlainText
        text: tile.name.trim().length > 0 ? Array.from(tile.name.trim())[0].toUpperCase() : "?"
        font.pixelSize: tile.width / 2
        font.bold: true
        color: Theme.heading
    }
}
