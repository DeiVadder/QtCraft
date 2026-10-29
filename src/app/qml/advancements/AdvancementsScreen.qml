pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The adventure guide as a tree (decision E-924), over the world: one tab per root, the
// advancements from left to right with lines from parent to child (places from AdvancementTree), the
// details of the one under the pointer or the focus beside the tree. The tree pans by dragging, the
// wheel and the pad's right stick, and zooms with Ctrl and the wheel or + and -; the arrow keys and
// the pad's directions walk along the tree (left: parent, right: child, up and down: the next one in
// the column). Opened by its key, the pause menu or --overlay advancements; Esc, its key or B close
// it (closeRequested).
Item {
    id: screen

    required property GameController game
    readonly property AdvancementTree tree: game.advancements
    // tree.nodes, read once per change: each read of the property converts the whole list.
    readonly property var nodes: tree.nodes
    // The chosen advancement's index in nodes (-1: none); the tree keeps it by name.
    readonly property int selected: tree.selectedIndex
    // The advancement that had the focus last: it gets it back when the tree changes under it.
    property string focusedName: ""
    property real zoom: 1.0
    readonly property real minimumZoom: 0.8
    readonly property real maximumZoom: 1.6
    readonly property real columnStep: Ui.size.advancementColumn * zoom
    readonly property real rowStep: Ui.size.advancementRow * zoom
    readonly property real nodeSize: Ui.size.advancementNode * zoom

    signal closeRequested

    // The neighbour of node `index` in the tree in a direction; -1 for none.
    function neighbour(index: int, dx: int, dy: int): int {
        const nodes = screen.nodes;
        const from = nodes[index];
        if (dx < 0)
            return from.parent;

        let best = -1;
        let bestDistance = 0;
        for (let i = 0; i < nodes.length; ++i) {
            const other = nodes[i];
            const inDirection = dx > 0 ? other.parent === index
                                       : other.column === from.column && (dy < 0 ? other.row < from.row : other.row > from.row);
            const distance = Math.abs(other.row - from.row);
            if (inDirection && (best < 0 || distance < bestDistance)) {
                best = i;
                bestDistance = distance;
            }
        }
        return best;
    }

    function focusNode(index: int): bool {
        const item = index >= 0 ? nodeItems.itemAt(index) : null;
        if (!item)
            return false;
        item.forceActiveFocus(Qt.TabFocusReason);
        return true;
    }

    // The index of the node with the focus; -1 when the focus is elsewhere.
    function focusedNode(): int {
        for (let i = 0; i < nodeItems.count; ++i) {
            if (nodeItems.itemAt(i)?.activeFocus)
                return i;
        }
        return -1;
    }

    function indexOf(name: string): int {
        return screen.nodes.findIndex(node => node.name === name);
    }

    // Whether `item` is a control inside the screen's frame (a button, a tab or an advancement), not
    // just the place the focus fell back to when its advancement went away.
    function onControl(item: Item): bool {
        if (!item || !item.activeFocusOnTab)
            return false;
        for (let at = item; at; at = at.parent) {
            if (at === frame)
                return true;
        }
        return false;
    }

    // After the tree changed: the advancement that had the focus gets it back, unless the focus went
    // to another control of the screen meanwhile.
    function restoreFocus() {
        if (!screen.visible || screen.focusedName.length === 0 || screen.onControl(screen.Window.activeFocusItem))
            return;
        const index = screen.indexOf(screen.focusedName);
        screen.focusNode(index >= 0 ? index : 0);
    }

    function step(dx: int, dy: int): bool {
        const from = focusedNode();
        return from >= 0 && focusNode(neighbour(from, dx, dy));
    }

    // Scrolls the view so node `index` shows whole, with a margin around it.
    function reveal(index: int) {
        const item = nodeItems.itemAt(index);
        if (!item)
            return;
        const margin = Ui.space.xl;
        const left = canvas.x + canvas.margin + item.x - margin;
        const top = canvas.y + canvas.margin + item.y - margin;
        const right = left + item.width + 2 * margin;
        const bottom = top + item.height + 2 * margin;
        const highestX = Math.max(0, view.contentWidth - view.width);
        const highestY = Math.max(0, view.contentHeight - view.height);
        if (left < view.contentX)
            view.contentX = Math.max(0, left);
        else if (right > view.contentX + view.width)
            view.contentX = Math.min(highestX, right - view.width);
        if (top < view.contentY)
            view.contentY = Math.max(0, top);
        else if (bottom > view.contentY + view.height)
            view.contentY = Math.min(highestY, bottom - view.height);
    }

    function openHandbook(target: string) {
        if (target.length > 0)
            game.handbook.open(target);
    }

    function setZoom(value: real) {
        // Zooms about the middle of the view.
        const centreX = (view.contentX + view.width / 2) / screen.zoom;
        const centreY = (view.contentY + view.height / 2) / screen.zoom;
        screen.zoom = Math.max(screen.minimumZoom, Math.min(screen.maximumZoom, value));
        // The content's size follows the zoom through the bindings first.
        const highestX = Math.max(0, Math.max(view.width, canvas.width) - view.width);
        const highestY = Math.max(0, Math.max(view.height, canvas.height) - view.height);
        view.contentX = Math.max(0, Math.min(highestX, centreX * screen.zoom - view.width / 2));
        view.contentY = Math.max(0, Math.min(highestY, centreY * screen.zoom - view.height / 2));
    }

    function switchTab(direction: int): bool {
        const next = tree.tab + direction;
        if (next < 0 || next >= tree.tabs.length)
            return false;
        tree.tab = next;
        Qt.callLater(screen.focusNode, 0);
        return true;
    }

    // The pad: B closes, the shoulders switch the tab, the directions walk along the tree.
    function gamepadCommand(command: string): bool {
        switch (command) {
        case "back":
            closeRequested();
            return true;
        case "tabNext":
            return switchTab(1);
        case "tabPrevious":
            return switchTab(-1);
        case "left":
            return step(-1, 0);
        case "right":
            return step(1, 0);
        case "up":
            return step(0, -1);
        case "down":
            return step(0, 1);
        default:
            return false;
        }
    }

    objectName: "advancementsScreen"
    focus: visible
    onVisibleChanged: {
        if (!visible)
            return;
        tree.update();
        tree.selected = "";
        screen.focusedName = "";
        Qt.callLater(screen.focusNode, 0);
    }
    Keys.onPressed: event => {
        // The key that opened it closes it, but not its auto-repeat while it is still held.
        if (event.key === Qt.Key_Escape || (game.settings.keys.matches("advancements", event.key) && !event.isAutoRepeat)) {
            screen.closeRequested();
        } else if (game.settings.keys.matches("handbook", event.key)) {
            screen.openHandbook(screen.selected >= 0 ? screen.nodes[screen.selected].handbook : "");
        } else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
            screen.setZoom(screen.zoom + 0.2);
        } else if (event.key === Qt.Key_Minus) {
            screen.setZoom(screen.zoom - 0.2);
        }
        // Other keys end here as well; Tab moves between the advancements and the buttons.
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    Connections {
        target: screen.tree

        function onChanged() {
            Qt.callLater(screen.restoreFocus);
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.scrim
    }

    ScreenFrame {
        id: frame

        anchors.horizontalCenter: parent.horizontalCenter
        y: Ui.screen.marginVertical
        width: Math.min(parent.width - 2 * Ui.screen.margin, Ui.screen.l)
        height: parent.height - 2 * Ui.screen.marginVertical - (screen.game.gamepad.focusShown ? Ui.size.padHints : 0)

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Ui.space.xl
            spacing: Ui.space.m

            RowLayout {
                Layout.fillWidth: true
                spacing: Ui.space.m

                Label {
                    text: qsTr("Advancements")
                    color: Theme.text
                    font.pixelSize: Ui.type.title
                    font.bold: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                Label {
                    objectName: "advancementCount"
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignBaseline
                    text: qsTr("%1 of %2 reached").arg(screen.tree.reached).arg(screen.tree.total)
                    color: Theme.textMuted
                    font.pixelSize: Ui.type.help
                    elide: Text.ElideRight
                }
                GameButton {
                    objectName: "advancementsClose"
                    kind: GameButton.Quiet
                    text: qsTr("Close")
                    keyHint: qsTr("Esc")
                    onClicked: screen.closeRequested()
                }
            }

            Segments {
                Layout.fillWidth: true
                visible: screen.tree.tabs.length > 1
                label: qsTr("Tree")
                model: screen.tree.tabs.map((tab, index) => ({ text: tab.title, value: index }))
                currentValue: screen.tree.tab
                onActivated: value => screen.switchTab(value - screen.tree.tab)
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Ui.space.m

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.field
                    radius: Ui.radius
                    clip: true

                    Flickable {
                        id: view

                        objectName: "advancementTree"
                        anchors.fill: parent
                        contentWidth: Math.max(width, canvas.width)
                        contentHeight: Math.max(height, canvas.height)
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ThemedScrollBar {}
                        ScrollBar.horizontal: ThemedScrollBar {}

                        // Ctrl and the wheel zoom; the wheel alone scrolls.
                        WheelHandler {
                            acceptedModifiers: Qt.ControlModifier
                            onWheel: event => screen.setZoom(screen.zoom * (event.angleDelta.y > 0 ? 1.1 : 1 / 1.1))
                        }

                        Item {
                            id: canvas

                            readonly property real margin: Ui.space.xl
                            readonly property real treeWidth: Math.max(0, screen.tree.columns - 1) * screen.columnStep + screen.nodeSize
                            readonly property real treeHeight: Math.max(0, screen.tree.rows - 1) * screen.rowStep + screen.nodeSize

                            // Centred while it is smaller than the view.
                            x: Math.max(0, (view.width - width) / 2)
                            y: Math.max(0, (view.height - height) / 2)
                            width: treeWidth + 2 * margin
                            height: treeHeight + 2 * margin

                            // The lines from each parent to its children: out of the parent, along
                            // the column gap, into the child; reached children in the accent.
                            Repeater {
                                model: screen.nodes

                                delegate: Item {
                                    id: link

                                    required property var modelData

                                    readonly property bool hasParent: modelData.parent >= 0
                                    readonly property real fromY: canvas.margin + modelData.parentRow * screen.rowStep + screen.nodeSize / 2
                                    readonly property real toY: canvas.margin + modelData.row * screen.rowStep + screen.nodeSize / 2
                                    readonly property real startX: canvas.margin + modelData.parentColumn * screen.columnStep + screen.nodeSize
                                    readonly property real middleX: startX + (screen.columnStep - screen.nodeSize) / 2
                                    readonly property real endX: canvas.margin + modelData.column * screen.columnStep
                                    readonly property real thickness: Ui.size.bevelDeep
                                    readonly property color lineColor: modelData.state === AdvancementTree.Reached ? Theme.accent : Theme.textMuted

                                    visible: hasParent

                                    Rectangle {
                                        x: link.startX
                                        y: link.fromY - link.thickness / 2
                                        width: link.middleX - link.startX
                                        height: link.thickness
                                        color: link.lineColor
                                    }
                                    Rectangle {
                                        x: link.middleX - link.thickness / 2
                                        y: Math.min(link.fromY, link.toY) - link.thickness / 2
                                        width: link.thickness
                                        height: Math.abs(link.toY - link.fromY) + link.thickness
                                        color: link.lineColor
                                    }
                                    Rectangle {
                                        x: link.middleX
                                        y: link.toY - link.thickness / 2
                                        width: link.endX - link.middleX
                                        height: link.thickness
                                        color: link.lineColor
                                    }
                                }
                            }

                            Item {
                                x: canvas.margin
                                y: canvas.margin

                                Repeater {
                                    id: nodeItems

                                    model: screen.nodes

                                    delegate: AdvancementNode {
                                        required property var modelData

                                        advancement: modelData
                                        columnStep: screen.columnStep
                                        rowStep: screen.rowStep
                                        size: screen.nodeSize
                                        zoom: screen.zoom
                                        onChosen: index => screen.tree.selected = screen.nodes[index].name
                                        onActivated: index => screen.openHandbook(screen.nodes[index].handbook)
                                        onStepped: (dx, dy) => screen.step(dx, dy)
                                        onFocused: index => {
                                            screen.focusedName = screen.nodes[index].name;
                                            screen.reveal(index);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                AdvancementDetails {
                    Layout.preferredWidth: Ui.size.advancementDetails
                    Layout.fillHeight: true
                    advancement: screen.selected >= 0 && screen.selected < screen.nodes.length ? screen.nodes[screen.selected] : undefined
                    onHandbookRequested: target => screen.openHandbook(target)
                }
            }
        }
    }
}
