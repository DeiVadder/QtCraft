// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtCraft

// One advancement in the tree (decision E-924): its icon on a tile whose shape names the frame (a
// square for a task, a rounded tile for a goal, a tile on a turned square, an eight-pointed star,
// for a challenge). Reached, partly reached and not reached differ in more than colour: a reached one
// carries a check mark, a partly reached one its progress ("2/4"), one not reached yet shows its icon
// faded. Hovering or focusing it chooses it for the details beside the tree; a click, Enter, Space
// or the pad's A opens its handbook page.
Item {
    id: node

    // An entry of AdvancementTree.nodes.
    required property var advancement
    required property int index
    // Pixels per column and per row, and the tile's side (the screen's zoom included).
    required property real columnStep
    required property real rowStep
    required property real size
    // The screen's zoom: the marks and the focus outline grow with the tile.
    required property real zoom

    // The pointer is on it or it has the focus: the details show it.
    signal chosen(int index)
    // Asks for its handbook page.
    signal activated(int index)
    // It took the focus: the view scrolls it into sight.
    signal focused(int index)
    // An arrow key: -1/1 in x or y (the screen picks the neighbour in the tree).
    signal stepped(int dx, int dy)

    readonly property bool reached: advancement.state === AdvancementTree.Reached
    readonly property bool partial: advancement.state === AdvancementTree.Partial
    readonly property color edge: reached ? Theme.accent : partial ? Theme.heading : Theme.panelBorder
    readonly property string stateText: reached ? qsTr("reached") : partial && advancement.progress.length > 0
                                                  ? qsTr("%1 reached").arg(advancement.progress) : qsTr("not reached yet")

    objectName: "advancement_" + advancement.name.replace(/:/g, "_")
    x: advancement.column * columnStep
    y: advancement.row * rowStep
    width: size
    height: size
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("%1, %2").arg(advancement.title).arg(stateText)
    Accessible.description: advancement.description
    Accessible.onPressAction: node.activated(node.index)
    onActiveFocusChanged: {
        if (activeFocus) {
            node.chosen(node.index);
            node.focused(node.index);
        }
    }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            node.activated(node.index);
        } else if (event.key === Qt.Key_Left) {
            node.stepped(-1, 0);
        } else if (event.key === Qt.Key_Right) {
            node.stepped(1, 0);
        } else if (event.key === Qt.Key_Up) {
            node.stepped(0, -1);
        } else if (event.key === Qt.Key_Down) {
            node.stepped(0, 1);
        } else {
            return;
        }
        event.accepted = true;
    }

    // A challenge: a turned square behind the tile makes the eight points.
    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.78
        height: width
        rotation: 45
        visible: node.advancement.frame === AdvancementTree.Challenge
        color: Theme.iconBackdrop
        border.color: node.edge
        border.width: Ui.size.bevelDeep
    }

    Rectangle {
        id: tile

        anchors.fill: parent
        anchors.margins: node.advancement.frame === AdvancementTree.Challenge ? parent.width * 0.08 : 0
        radius: node.advancement.frame === AdvancementTree.Goal ? width / 3 : Ui.radius
        color: hover.hovered ? Theme.slotHover : Theme.iconBackdrop
        border.color: node.edge
        border.width: node.reached || node.partial ? 2 * Ui.size.bevelDeep : Ui.size.bevelDeep

        Image {
            anchors.centerIn: parent
            width: parent.width * 0.6
            height: width
            source: ItemIcons.source(node.advancement.icon, ItemIcons.generation)
            sourceSize: Qt.size(32, 32)
            smooth: false
            opacity: node.reached || node.partial ? 1.0 : 0.5
        }
    }

    // Under the marks, so they stay readable on a focused advancement.
    FocusOutline {
        anchors.margins: -2 * Ui.size.focus * node.zoom
        border.width: Ui.size.focus * node.zoom
        visible: node.activeFocus
    }

    // Reached: a check mark at the top right.
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: -Ui.space.xs * node.zoom
        width: Ui.size.badge * node.zoom
        height: width
        radius: width / 2
        visible: node.reached
        color: Theme.accent

        Label {
            anchors.centerIn: parent
            text: "✓"
            color: Theme.accentText
            font.pixelSize: Math.round(Ui.type.badge * node.zoom)
            font.bold: true
        }
    }

    // Partly reached: the requirement groups met, below the tile.
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.bottom
        width: progressLabel.implicitWidth + 2 * Ui.space.xs * node.zoom
        height: Ui.size.badge * node.zoom
        radius: Ui.radius
        visible: node.partial && node.advancement.progress.length > 0
        color: Theme.field
        border.color: Theme.heading
        border.width: Ui.size.line

        Label {
            id: progressLabel

            anchors.centerIn: parent
            text: node.advancement.progress
            color: Theme.heading
            font.pixelSize: Math.round(Ui.type.badge * node.zoom)
            font.bold: true
        }
    }

    HoverHandler {
        id: hover

        onHoveredChanged: {
            if (hovered)
                node.chosen(node.index);
        }
    }

    TapHandler {
        onTapped: node.activated(node.index)
    }
}
