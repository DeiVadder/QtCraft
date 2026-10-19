pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Screen of an open storage block (chest, barrel, hopper, dispenser; SlotScreen): its slots above
// the inventory, in rows as long as the block's data says.
SlotScreen {
    id: screen

    readonly property int firstStorageRow: 46

    panelContent: content

    ColumnLayout {
        id: content

        anchors.centerIn: parent
        spacing: 12

        Label {
            textFormat: Text.PlainText
            text: screen.game.player.machineLabel.length > 0 ? screen.game.player.machineLabel : qsTr("Chest")
            color: Theme.text
            font.pixelSize: 24
            font.bold: true
        }

        GridLayout {
            objectName: "storage"
            columns: screen.game.player.containerColumns
            rowSpacing: 2
            columnSpacing: 2

            Repeater {
                model: SlotRange {
                    source: screen.game.player.inventory
                    first: screen.firstStorageRow
                    count: screen.game.player.containerSlots
                }

                InventorySlot {
                    readonly property int storageIndex: slot - screen.firstStorageRow

                    owner: screen
                    Layout.row: Math.floor(storageIndex / screen.game.player.containerColumns)
                    Layout.column: storageIndex % screen.game.player.containerColumns
                }
            }
        }

        PlayerSlots {
            owner: screen
        }

        Label {
            Layout.maximumWidth: 9 * 54
            wrapMode: Text.Wrap
            text: screen.game.gamepad.active ? qsTr("A: pick up and put down. X: put down one. Y: move the stack between this block and the inventory. B closes. Breaking the block drops its contents.")
                  : qsTr("Left click: pick up and put down. Right click: put down one. Shift-click: move the stack between this block and the inventory. %1 or Esc closes. Breaking the block drops its contents.").arg(screen.game.settings.keys.labels.inventory)
            color: Theme.textSecondary
        }
    }
}
