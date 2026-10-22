import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// One saved world in the main menu's list (decision E-852): its tile, name and summary (when it was
// played, its type and mode), or why it cannot be opened, with Play and Delete on the right; a
// double click plays it too.
ListRow {
    id: row

    // An entry of GameController.library.worlds.
    required property var modelData
    // What the subtitle says about the world (WorldTexts.summary).
    property string summary

    signal playRequested(string folder)
    signal deleteRequested(string folder, string name)

    interactive: true
    title: modelData.name
    subtitle: modelData.error.length > 0 ? "" : summary
    Accessible.role: Accessible.ListItem
    Accessible.name: modelData.name

    // A double click plays the world, as Play does.
    onDoubleClicked: {
        if (row.modelData.error.length === 0)
            row.playRequested(row.modelData.folder);
    }

    leading: WorldTile {
        implicitWidth: Ui.size.thumbnailSmall
        name: row.modelData.name
    }

    Label {
        Layout.fillWidth: true
        visible: row.modelData.error.length > 0
        textFormat: Text.PlainText
        text: row.modelData.error
        font.pixelSize: Ui.type.help
        color: Theme.error
        wrapMode: Text.Wrap
    }

    actions: RowLayout {
        spacing: Ui.space.s

        GameButton {
            text: qsTr("Play")
            Accessible.name: qsTr("Play %1").arg(row.modelData.name)
            enabled: row.modelData.error.length === 0
            onClicked: row.playRequested(row.modelData.folder)
        }
        GameButton {
            kind: GameButton.Quiet
            text: qsTr("Delete")
            Accessible.name: qsTr("Delete %1").arg(row.modelData.name)
            onClicked: row.deleteRequested(row.modelData.folder, row.modelData.name)
        }
    }
}
