// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Base of the screens with slots (inventory and crafting, chest, furnace): the picked-up stack
// following the mouse, the dimmed source slot, the panel behind the content and the keys. Rows are
// those of the inventory model. What a click does (pick up, put down, right click, shift-click,
// the crafting result, a machine's output, the creative catalogue) decides the slot cursor in C++
// (GameController.player.slotCursor, QcSlotCursor); the slots only forward their clicks to it. A
// click outside the slots empties the hand.
//
// Every slot screen has the same frame (draft D.2 A of docs/Menue-Pruefung.md, stage 76.2-e): the
// head with the title and "Close" with its key, the machine's part (the screen's own items) above
// the inventory with its heading, which always stands in the same place, optional columns beside
// them (recipes, offers, the creative catalogue), and a foot with the mouse's hints as key plates
// and the way to the machine's page in the handbook. The pad's hints stand at the window's bottom
// edge (PadHints) instead of the key plates.
Rectangle {
    id: screen

    objectName: "slotScreen"
    required property GameController game
    // The hand: the picked-up row, the carried catalogue item; the slots' clicks go to it.
    readonly property SlotCursor cursor: game.player.slotCursor
    // Item under the mouse; the handbook key opens its page.
    property string hoveredItem
    // Where the picked-up stack is drawn while the mouse is outside the window: next to its slot.
    property point pickedFrom

    // The screen's title in its head.
    property string title
    // The entry "More in the handbook" opens (an item or block name); empty: no link.
    property string handbookTarget
    // The mouse's hints in the foot ({keys, text}, KeyHintRow); the usual three by default.
    property var keyHints: [
        { keys: [qsTr("Click")], text: qsTr("take, put down") },
        { keys: [qsTr("Right click")], text: qsTr("one") },
        { keys: [qsTr("Shift-click")], text: qsTr("move across") }
    ]
    // Only the hotbar below the machine (a backpack opened from the hand, decision E-833).
    property bool hotbarOnly
    // The creative catalogue and the inventory switch between each other in their heads.
    property bool creativeSwitch
    // The machine's part above the inventory and the columns before and after it.
    default property alias machine: machineArea.data
    property alias startColumn: startArea.data
    property alias endColumn: endArea.data

    color: Theme.scrim
    focus: visible
    // The hand is emptied in C++ whenever the overlay changes.
    onVisibleChanged: {
        if (visible)
            forceActiveFocus();
    }
    Keys.onPressed: event => {
        const action = screen.game.settings.keys.screenKey(event.key, event.isAutoRepeat, KeyControls.ClosingKey.InventoryKey);
        if (action === KeyControls.ScreenKey.Close)
            screen.game.overlay = GameController.Overlay.None;
        else if (action === KeyControls.ScreenKey.Handbook)
            screen.game.handbook.open(screen.hoveredItem);
        // Other keys end here as well; Tab moves the focus.
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    // A click beside the slots puts the stack back: nothing a screen reader offers.
    MouseArea {
        anchors.fill: parent
        Accessible.ignored: true
        onClicked: screen.cursor.reset()
    }

    // Opaque panel so the crosshair and the world do not show through the slots.
    ScreenFrame {
        anchors.fill: frame
        anchors.margins: -Ui.space.l
    }

    ColumnLayout {
        id: frame

        objectName: "slotFrame"
        anchors.centerIn: parent
        spacing: Ui.space.s

        ScreenHeader {
            Layout.fillWidth: true
            title: screen.title
            closeName: "slotScreenClose"
            keyboardKey: screen.game.settings.keys.labels.inventory
            padKey: screen.game.gamepad.active ? screen.game.gamepad.pads.texts.east : ""
            onCloseRequested: screen.game.overlay = GameController.Overlay.None
        }

        Segments {
            objectName: "creativeSwitch"
            Layout.fillWidth: false
            visible: screen.creativeSwitch && screen.game.player.creative
            label: qsTr("Creative inventory")
            model: [
                { text: qsTr("Creative items"), value: GameController.Overlay.Creative },
                { text: qsTr("Crafting and armour"), value: GameController.Overlay.Inventory }
            ]
            currentValue: screen.game.overlay
            onActivated: value => screen.game.overlay = value
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Ui.size.line
            color: Theme.separator
        }

        RowLayout {
            spacing: Ui.space.l

            ColumnLayout {
                id: startArea

                Layout.alignment: Qt.AlignTop
                Layout.fillHeight: true
                visible: children.length > 0
                spacing: Ui.space.s
            }

            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                spacing: Ui.space.s

                ColumnLayout {
                    id: machineArea

                    Layout.alignment: Qt.AlignHCenter
                    spacing: Ui.space.m
                }

                Label {
                    text: qsTr("Inventory")
                    color: Theme.heading
                    font.pixelSize: Ui.type.group
                    font.bold: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                PlayerSlots {
                    owner: screen
                    hotbarOnly: screen.hotbarOnly
                }
            }

            ColumnLayout {
                id: endArea

                Layout.alignment: Qt.AlignTop
                Layout.fillHeight: true
                visible: children.length > 0
                spacing: Ui.space.s
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Ui.size.line
            color: Theme.separator
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Ui.space.l

            // The pad's buttons stand at the window's bottom edge while it leads (PadHints).
            KeyHintRow {
                objectName: "slotKeyHints"
                Layout.fillWidth: true
                opacity: screen.game.gamepad.active ? 0 : 1
                entries: screen.keyHints
            }
            GameButton {
                objectName: "slotHandbookLink"
                visible: screen.handbookTarget.length > 0
                kind: GameButton.Quiet
                text: qsTr("More in the handbook ›")
                onClicked: screen.game.handbook.open(screen.handbookTarget)
            }
        }
    }

    HoverHandler {
        id: pointer
    }

    // The catalogue item carried in creative mode, at the mouse pointer.
    ItemSlot {
        objectName: "carriedCatalogueItem"
        z: 100
        x: (pointer.hovered ? pointer.point.position.x : screen.width / 2) - width / 2
        y: (pointer.hovered ? pointer.point.position.y : screen.height / 2) - height / 2
        visible: screen.cursor.carriedItem.length > 0
        enabled: false
        carried: true
        slotIcon: visible ? ItemIcons.source(screen.cursor.carriedItem, ItemIcons.generation) : ""
        slotCount: screen.cursor.carriedCount
        slotEmpty: !visible
    }

    // The picked-up stack at the mouse pointer, over everything else of the screen.
    Repeater {
        model: SlotRange {
            source: screen.game.player.inventory
            first: Math.max(0, screen.cursor.pickedSlot)
            count: screen.cursor.pickedSlot >= 0 ? 1 : 0
        }

        ItemSlot {
            required property string icon
            required property int count
            required property real durability
            required property bool empty

            objectName: "carriedStack"
            z: 100
            x: (pointer.hovered ? pointer.point.position.x : screen.pickedFrom.x) - width / 2
            y: (pointer.hovered ? pointer.point.position.y : screen.pickedFrom.y) - height / 2
            visible: !empty
            enabled: false
            carried: true
            slotIcon: icon
            slotCount: count
            slotDurability: durability
            slotEmpty: empty
        }
    }
}
