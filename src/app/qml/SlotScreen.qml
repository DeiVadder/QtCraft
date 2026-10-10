pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtCraft

// Base of the screens with slots (inventory and crafting, chest, furnace): picking a stack up and
// putting it down, shift-click, the picked-up stack following the mouse, the dimmed source slot,
// the panel behind the content and the keys. Rows are those of the inventory model.
//
// Left click picks a stack up and puts it down elsewhere (merging or swapping); right click puts
// down a single item and keeps the rest picked up; shift-click moves the whole stack to where it
// belongs (decided by the server). A click outside the slots drops the picked-up stack back.
Rectangle {
    id: screen

    objectName: "slotScreen"
    required property GameController game
    property int pickedSlot: -1
    // Item under the mouse; the handbook key opens its page.
    property string hoveredItem
    // The crafting result: clicks take it (shift: as often as possible) instead of picking it up.
    property int resultSlot: -1
    // A machine's output: it can be picked up, nothing can be put into it.
    property int outputSlot: -1
    // The content the panel is drawn behind.
    property Item panelContent
    // Where the picked-up stack is drawn while the mouse is outside the window: next to its slot.
    property point pickedFrom

    color: Theme.scrim
    focus: visible
    onVisibleChanged: {
        pickedSlot = -1;
        if (visible)
            forceActiveFocus();
    }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape || screen.game.settings.keys.matches("inventory", event.key)) {
            screen.game.overlay = "";
        } else if (screen.game.settings.keys.matches("handbook", event.key)) {
            screen.game.handbook.open(screen.hoveredItem);
        }
        // Other keys end here as well; Tab moves the focus.
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    function clicked(slot, empty, button, quick) {
        if (slot === resultSlot) {
            if (empty)
                return;
            if (quick)
                game.player.quickMove(slot);
            else
                game.player.takeCraftingResult();
            return;
        }

        if (pickedSlot < 0) {
            if (empty)
                return;
            if (quick)
                game.player.quickMove(slot);
            else
                pickedSlot = slot;
            return;
        }

        if (pickedSlot !== slot && slot !== outputSlot)
            game.player.moveItem(pickedSlot, slot, button === Qt.RightButton ? 1 : 0);
        // Right click keeps the stack picked up so several cells can be filled one by one.
        if (button !== Qt.RightButton || slot === outputSlot)
            pickedSlot = -1;
    }

    MouseArea {
        anchors.fill: parent
        onClicked: screen.pickedSlot = -1
    }

    // Opaque panel so the crosshair and the world do not show through the slots.
    Rectangle {
        anchors.fill: screen.panelContent
        anchors.margins: -20
        visible: screen.panelContent !== null
        radius: 6
        color: Theme.panel
        border.color: Theme.panelBorder
    }

    HoverHandler {
        id: pointer
    }

    // The picked-up stack at the mouse pointer, over everything else of the screen.
    Repeater {
        model: SlotRange {
            source: screen.game.player.inventory
            first: Math.max(0, screen.pickedSlot)
            count: screen.pickedSlot >= 0 ? 1 : 0
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
            // The stack is gone (all put down one by one, or taken by someone else): nothing left
            // to carry.
            onSlotEmptyChanged: if (slotEmpty) screen.pickedSlot = -1
        }
    }
}
