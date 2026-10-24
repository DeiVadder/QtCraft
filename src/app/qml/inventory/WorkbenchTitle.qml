import QtQuick
import QtQuick.Controls
import QtCraft

// The title of a workbench screen (enchanting table, anvil; decision E-884): the block's icon and
// its name in the pixel font Pixelify Sans, like the runes and the logo.
Row {
    id: title

    property string text
    // The block whose icon stands before the name ("qtcraft:anvil").
    property string item

    spacing: Ui.space.s
    Accessible.role: Accessible.Heading
    Accessible.name: text

    Image {
        anchors.verticalCenter: parent.verticalCenter
        width: Ui.size.button
        height: Ui.size.button
        source: ItemIcons.source(title.item, ItemIcons.generation)
        smooth: false
        Accessible.ignored: true
    }

    Label {
        anchors.verticalCenter: parent.verticalCenter
        text: title.text
        color: Theme.text
        font.family: Ui.pixelFont.name
        font.pixelSize: Ui.type.title
        Accessible.ignored: true
    }
}
