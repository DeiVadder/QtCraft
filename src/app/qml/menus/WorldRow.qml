// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// One saved world in the main menu's list (decision E-852): its tile, name and summary (when it was
// played, its type and mode), or why it cannot be opened, with Play and a "…" menu (open its folder,
// delete it) on the right, so deleting is no longer a button as plain as Play beside it (HM-2 of
// docs/Menue-Pruefung.md); a double click plays it too. The row takes the focus itself (Tab, the
// pad): Enter and the pad's A play it, the pad's up and down walk the list row by row, right
// reaches its buttons (HM-6).
ListRow {
    id: row

    // An entry of GameController.library.worlds.
    required property var modelData
    // The row's place in the list.
    required property int index
    // What the subtitle says about the world (WorldTexts.summary).
    property string summary

    signal playRequested(string folder)
    signal deleteRequested(string folder, string name)
    signal openFolderRequested(string folder)

    // Plays the world unless it cannot be opened.
    function play() {
        if (row.modelData.error.length === 0)
            row.playRequested(row.modelData.folder);
    }

    objectName: "worldRow" + index
    interactive: true
    focusable: true
    listIndex: index
    title: modelData.name
    subtitle: modelData.error.length > 0 ? "" : summary
    Accessible.role: Accessible.ListItem
    Accessible.name: modelData.name

    // A double click and Enter play the world, as Play does (a single click does not).
    onDoubleClicked: play()
    onActivated: play()

    leading: WorldTile {
        implicitWidth: Ui.size.thumbnailSmall
        name: row.modelData.name
        typeBlock: row.modelData.typeBlock
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
            id: moreButton

            objectName: "worldMore" + row.index
            // A square plate beside Play (Secondary: raised fill, border, hover and focus ring),
            // so the menu stands out against the row.
            Layout.preferredWidth: implicitHeight
            kind: GameButton.Secondary
            leftPadding: 0
            rightPadding: 0
            text: "…"
            Accessible.name: qsTr("More for %1").arg(row.modelData.name)
            onClicked: moreMenu.open()

            Popup {
                id: moreMenu

                y: moreButton.height
                x: moreButton.width - width
                padding: Ui.space.xs
                focus: true
                background: ScreenFrame {}

                contentItem: ColumnLayout {
                    spacing: Ui.space.xs

                    GameButton {
                        objectName: "worldOpenFolder" + row.index
                        Layout.fillWidth: true
                        kind: GameButton.Quiet
                        text: qsTr("Open folder")
                        focus: true
                        onClicked: {
                            moreMenu.close();
                            row.openFolderRequested(row.modelData.folder);
                        }
                    }
                    GameButton {
                        objectName: "worldDelete" + row.index
                        Layout.fillWidth: true
                        kind: GameButton.Quiet
                        text: qsTr("Delete …")
                        Accessible.name: qsTr("Delete %1").arg(row.modelData.name)
                        onClicked: {
                            moreMenu.close();
                            row.deleteRequested(row.modelData.folder, row.modelData.name);
                        }
                    }
                }
            }
        }
    }
}
