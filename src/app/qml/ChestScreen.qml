pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Screen of an open storage block (chest, SlotScreen): its slots above the inventory.
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
            columns: 9
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
                    Layout.row: Math.floor(storageIndex / 9)
                    Layout.column: storageIndex % 9
                }
            }
        }

        PlayerSlots {
            owner: screen
        }

        Label {
            Layout.maximumWidth: 9 * 54
            wrapMode: Text.Wrap
            text: qsTr("Left click: pick up and put down. Right click: put down one. Shift-click: move the stack between chest and inventory. %1 or Esc closes. Breaking the chest drops its contents.").arg(screen.game.settings.keys.labels.inventory)
            color: Theme.textSecondary
        }
    }
}
