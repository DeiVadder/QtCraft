// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtCraft

// A world's tile in the main menu (decision E-852): a raised block with the icon of a block that
// stands for the world's type (grass for a normal world, a slab for a flat one, glass for the void,
// a bookshelf for the showcase; HM-3 of docs/Menue-Pruefung.md), standing in for a picture of the
// world (worlds keep none); the first letter of its name while the icons are not drawn yet or
// the icon cannot be shown.
Rectangle {
    id: tile

    // The first letter of the world's name and the block whose icon stands for its type (from the
    // library).
    property string initial
    property string typeBlock
    // Empty while nothing has drawn icons yet (in the menu before the first world or handbook).
    readonly property string typeIcon: ItemIcons.generation > 0 && typeBlock.length > 0
                                       ? ItemIcons.source(typeBlock, ItemIcons.generation) : ""

    implicitWidth: Ui.size.thumbnail
    implicitHeight: implicitWidth
    radius: Ui.radius
    color: Theme.panelRaised
    Accessible.ignored: true

    Bevel {}

    Image {
        id: icon

        anchors.fill: parent
        anchors.margins: parent.width / 6
        visible: tile.typeIcon.length > 0 && status !== Image.Error
        source: tile.typeIcon
        sourceSize: Qt.size(width, height)
        smooth: false
    }

    Label {
        anchors.centerIn: parent
        visible: !icon.visible
        textFormat: Text.PlainText
        text: tile.initial
        font.pixelSize: tile.width / 2
        font.bold: true
        color: Theme.heading
    }
}
