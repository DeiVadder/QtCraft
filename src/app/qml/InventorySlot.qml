import QtQuick
import QtQuick.Controls
import QtCraft

// A slot of a SlotScreen, filled from the inventory model's roles: clicks, shift-clicks, hover for
// the handbook key, a tooltip, and the dimmed look while its stack is picked up.
ItemSlot {
    id: slotItem

    required property SlotScreen owner
    required property int slot
    required property string icon
    required property int count
    required property real durability
    required property bool empty
    required property string label
    required property string itemName

    // Clicks on slots sound like buttons (QcGameInput).
    property bool clickSound: true

    infoItem: itemName
    infoEnabled: owner.pickedSlot < 0
    onInfoRequested: item => owner.game.handbook.open(item)
    slotIcon: icon
    slotCount: count
    slotDurability: durability
    slotEmpty: empty
    selected: slot === owner.pickedSlot
    picked: slot === owner.pickedSlot
    onPickedChanged: {
        if (picked)
            owner.pickedFrom = mapToItem(owner, width * 0.75, height * 0.75);
    }
    highlighted: hover.hovered

    ToolTip.visible: hover.hovered && !empty && owner.pickedSlot < 0
    ToolTip.text: label

    HoverHandler {
        id: hover

        onHoveredChanged: slotItem.owner.hoveredItem = hovered && !slotItem.empty ? slotItem.itemName : ""
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        acceptedModifiers: Qt.NoModifier
        onTapped: (eventPoint, button) => slotItem.owner.clicked(slotItem.slot, slotItem.empty, button, false)
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        acceptedModifiers: Qt.ShiftModifier
        onTapped: (eventPoint, button) => slotItem.owner.clicked(slotItem.slot, slotItem.empty, button, true)
    }
}
