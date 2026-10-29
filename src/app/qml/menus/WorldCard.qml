import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The card of the pause menu and the death screen (Menue-Pruefung D.1 A, TD-1, decision E-928): a
// ScreenFrame `Ui.screen.s` wide, in the middle of the room above `reservedBottom` (the HUD's status
// rows, which stay visible behind these screens (decision E-927), or the pad's hints). Taller than
// that room, its content scrolls inside the card, and the item with the focus is scrolled into view
// (Tab and the pad alike).
ScreenFrame {
    id: card

    // The height at the bottom of the parent the card leaves free.
    property real reservedBottom: 0
    default property alias content: column.data
    readonly property real room: parent.height - reservedBottom - 2 * Ui.screen.marginVertical

    // Scrolls the content so that `item` shows whole, with a small margin.
    function reveal(item: Item) {
        if (!item || !view.interactive)
            return;

        const top = item.mapToItem(view.contentItem, 0, 0).y;
        if (top < view.contentY)
            view.contentY = Math.max(0, top - Ui.space.s);
        else if (top + item.height > view.contentY + view.height)
            view.contentY = Math.min(view.contentHeight - view.height, top + item.height - view.height + Ui.space.s);
    }

    objectName: "worldCard"
    anchors.horizontalCenter: parent.horizontalCenter
    y: Ui.screen.marginVertical + Math.max(0, (room - height) / 2)
    width: Math.min(parent.width - 2 * Ui.screen.margin, Ui.screen.s)
    height: Math.max(0, Math.min(column.implicitHeight + 2 * Ui.space.xl, room))

    Flickable {
        id: view

        objectName: "worldCardView"
        anchors.fill: parent
        anchors.margins: Ui.space.xl
        // The scroll bar runs in a gutter of the right padding.
        anchors.rightMargin: Ui.space.s
        clip: true
        contentWidth: width
        contentHeight: column.implicitHeight
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ThemedScrollBar {}

        ColumnLayout {
            id: column

            width: view.width - (Ui.space.xl - Ui.space.s)
            spacing: Ui.space.xl
        }
    }

    Connections {
        target: card.Window.window

        function onActiveFocusItemChanged() {
            const item = card.Window.activeFocusItem;
            for (let at = item; at; at = at.parent) {
                if (at === column) {
                    card.reveal(item);
                    return;
                }
            }
        }
    }
}
