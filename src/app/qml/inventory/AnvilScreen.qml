// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The anvil's screen (SlotScreen, decisions E-866 and E-884), laid out like the reference's: the
// name field of parchment above, the left slot, a plus, the right slot, an arrow (crossed out while
// the work is too expensive) and the output in one row, the cost in levels and the button that takes
// the output (it goes into the left slot), and the inventory below. Built from the same workbench
// parts as the enchanting table (ParchmentField, MachineSlotRow, PixelArt, LevelBadge). The
// slots are the inventory model's machine rows; the server decides and sends them back.
SlotScreen {
    id: screen

    readonly property var anvil: game.anvil
    readonly property int leftRow: InventoryModel.MachineFirst

    title: qsTr("Anvil")
    handbookTarget: "qtcraft:anvil"

    // The cost line: what the output costs, or why there is none.
    function costText(): string {
        if (!anvil.hasOutput)
            return qsTr("Nothing to forge")
        if (anvil.tooExpensive)
            return qsTr("Too expensive (%n level(s))", "", anvil.levelCost)
        return qsTr("Cost: %n level(s)", "", anvil.levelCost)
    }

    GameTextField {
        id: nameField
        objectName: "anvilName"
        Accessible.name: qsTr("Name")
        Layout.preferredWidth: Ui.size.offerWidth
        Layout.alignment: Qt.AlignHCenter
        maximumLength: screen.anvil.maxNameLength
        placeholderText: qsTr("Name (empty: no custom name)")
        color: Theme.ink
        placeholderTextColor: Theme.parchmentDisabled
        font.pixelSize: Ui.type.body
        // The field is the screen's: it takes the left stack's name when the screen opens or
        // another stack lies there; what the player types goes to the server one way.
        readonly property int resets: screen.anvil.nameResets
        onResetsChanged: text = screen.anvil.leftName
        Component.onCompleted: text = screen.anvil.leftName
        onTextEdited: screen.anvil.rename(text)

        background: ParchmentField {
            implicitWidth: Ui.size.value
            hovered: nameField.hovered

            FocusOutline {
                visible: nameField.activeFocus
            }
        }
    }

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: Ui.space.m

        MachineSlotRow {
            owner: screen
            first: screen.leftRow
            names: ["anvilLeft"]
        }

        PixelArt {
            Layout.alignment: Qt.AlignVCenter
            picture: "plus/textSecondary"
        }

        MachineSlotRow {
            owner: screen
            first: screen.leftRow + 1
            names: ["anvilRight"]
        }

        Item {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: arrow.width
            implicitHeight: arrow.height

            PixelArt {
                id: arrow
                picture: "arrow/textSecondary"
            }

            PixelArt {
                objectName: "anvilCross"
                anchors.centerIn: arrow
                visible: screen.anvil.tooExpensive
                picture: "cross/error"
            }
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

    // What goes where (IV-8 of docs/Menue-Pruefung.md); the rest is in the handbook.
    Label {
        Layout.alignment: Qt.AlignHCenter
        text: qsTr("Left: the item · right: its material or a second one")
        color: Theme.textMuted
        font.pixelSize: Ui.type.help
    }

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: Ui.space.m

        LevelBadge {
            Layout.alignment: Qt.AlignVCenter
            text: screen.costText()
            available: screen.anvil.hasOutput
            missing: screen.anvil.hasOutput && !screen.anvil.affordable
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        GameButton {
            objectName: "anvilForge"
            enabled: screen.anvil.affordable
            text: qsTr("Forge")
            onClicked: screen.anvil.forge()
        }
    }
}
