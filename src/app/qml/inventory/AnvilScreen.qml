pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The anvil's screen (SlotScreen, decision E-866): the name field above, the left and right slots
// with the output beside them, the cost and the button that takes the output (it goes into the left
// slot), and the inventory below. The slots are the inventory model's machine rows; the server
// decides and sends them back.
SlotScreen {
    id: screen

    readonly property var anvil: game.anvil
    readonly property int leftRow: 46

    panelContent: content

    // The cost line: what the output costs, or why there is none.
    function costText(): string {
        if (!anvil.hasOutput)
            return qsTr("Nothing to forge")
        if (anvil.tooExpensive)
            return qsTr("Too expensive (%n level(s))", "", anvil.levelCost)
        return qsTr("Cost: %n level(s)", "", anvil.levelCost)
    }

    ColumnLayout {
        id: content
        anchors.centerIn: parent
        spacing: Ui.space.m

        Label {
            text: qsTr("Anvil")
            color: Theme.text
            font.pixelSize: Ui.type.title
            font.bold: true
        }

        GameTextField {
            id: nameField
            objectName: "anvilName"
            Accessible.name: qsTr("Name")
            Layout.preferredWidth: Ui.size.formColumn
            maximumLength: screen.anvil.maxNameLength
            placeholderText: qsTr("Name (empty: no custom name)")
            // The field is the screen's: it takes the left stack's name when the screen opens or
            // another stack lies there; what the player types goes to the server one way.
            readonly property int resets: screen.anvil.nameResets
            onResetsChanged: text = screen.anvil.leftName
            Component.onCompleted: text = screen.anvil.leftName
            onTextEdited: screen.anvil.rename(text)
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Ui.space.s

            Repeater {
                model: SlotRange {
                    source: screen.game.player.inventory
                    first: screen.leftRow
                    count: 2
                }

                InventorySlot {
                    required property int index
                    objectName: index === 0 ? "anvilLeft" : "anvilRight"
                    owner: screen
                }
            }

            Label {
                text: "→"
                color: Theme.textSecondary
                font.pixelSize: Ui.type.title
            }

            Repeater {
                model: SlotRange {
                    source: screen.game.player.inventory
                    first: screen.leftRow + 2
                    count: 1
                }

                ItemSlot {
                    required property string icon
                    required property int count
                    required property real durability
                    required property bool empty
                    required property string label
                    objectName: "anvilOutput"
                    slotIcon: icon
                    slotCount: count
                    slotDurability: durability
                    slotEmpty: empty
                    slotLabel: label
                    infoEnabled: false
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Ui.space.m

            Label {
                objectName: "anvilCost"
                text: screen.costText()
                color: screen.anvil.hasOutput && !screen.anvil.affordable ? Theme.error : Theme.textSecondary
                font.pixelSize: Ui.type.body
            }

            GameButton {
                objectName: "anvilForge"
                enabled: screen.anvil.affordable
                text: qsTr("Forge")
                onClicked: screen.anvil.forge()
            }
        }

        PlayerSlots {
            owner: screen
        }

        Label {
            Layout.maximumWidth: Ui.size.formColumn * 2
            wrapMode: Text.Wrap
            text: screen.game.gamepad.active ? qsTr("Put an item into the left slot and its material or a second item of the same kind into the right one to repair it and merge enchantments, or type a new name. Forge puts the result into the left slot. %1: pick up and put down. %2: put down one. %3: move the stack across. %4 closes.")
                    .arg(screen.game.gamepad.pads.texts.south).arg(screen.game.gamepad.pads.texts.west).arg(screen.game.gamepad.pads.texts.north).arg(screen.game.gamepad.pads.texts.east)
                  : qsTr("Put an item into the left slot and its material or a second item of the same kind into the right one to repair it and merge enchantments, or type a new name. Forge puts the result into the left slot. Left click: pick up and put down. Right click: put down one. Shift-click: move the stack across. %1 or Esc closes.").arg(screen.game.settings.keys.labels.inventory)
            color: Theme.textSecondary
            font.pixelSize: Ui.type.help
        }
    }
}
