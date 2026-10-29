// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtCraft

// Labels floating in the world (the names in the showcase world, the debug display's light values):
// drawn where the game puts them on the view every frame, nearer ones larger, fading out with
// distance; the label key switches the names. Sizes follow the HUD scale (Ui.hudPx, decision E-725).
// A label is no wider than the space to its neighbour (labelMaxWidth): longer lines end in "…",
// the detail line is smaller than the name.
// Labels stay inside the view (decision E-720): one that would reach over an edge is moved back in,
// and one that comes down to the HUD's status rows (hudArea) fades out before it reaches them.
Item {
    id: layer

    // A QcWorldLabelModel.
    required property var labels
    // The status rows and hotbar in this item's coordinates; empty: nothing to keep clear.
    property rect hudArea: Qt.rect(0, 0, 0, 0)
    // Distance kept from the edges of the view.
    readonly property real edgeMargin: Ui.hudPx(4)
    // Height over which a label fades out above hudArea.
    readonly property real hudFade: Ui.hudPx(24)

    Repeater {
        model: layer.labels

        delegate: Rectangle {
            id: worldLabel

            required property string title
            required property string detail
            required property bool heading
            required property real labelX
            required property real labelY
            required property real labelScale
            required property real labelOpacity
            required property real labelMaxWidth

            readonly property real horizontalPadding: Ui.hudPx(6)
            readonly property real lineLimit: heading ? Number.POSITIVE_INFINITY
                                                      : Math.max(Ui.hudPx(32), labelMaxWidth - 2 * horizontalPadding)

            // The label as drawn: scaled about its bottom centre, moved inside the view.
            readonly property real drawnWidth: width * labelScale
            readonly property real drawnHeight: height * labelScale
            // A label wider than the view stays centred on it.
            readonly property real centreX: drawnWidth > layer.width - 2 * layer.edgeMargin
                                            ? layer.width / 2
                                            : Math.max(layer.edgeMargin + drawnWidth / 2,
                                                       Math.min(layer.width - layer.edgeMargin - drawnWidth / 2, labelX))
            // QcLabelProjector keeps anchors up to 5 % outside the view: one just below it is lifted in.
            readonly property real bottomY: Math.min(layer.height - layer.edgeMargin,
                                                     Math.max(layer.edgeMargin + drawnHeight, labelY))
            // 1 while the label is clear of the HUD, down to 0 when its top reaches hudArea.
            readonly property bool overHud: layer.hudArea.width > 0 && centreX + drawnWidth / 2 > layer.hudArea.x
                                            && centreX - drawnWidth / 2 < layer.hudArea.x + layer.hudArea.width
            readonly property real hudVisibility: overHud ? 1 - Math.max(0, Math.min(1, (bottomY - layer.hudArea.y + layer.hudFade)
                                                                                       / (layer.hudFade + drawnHeight)))
                                                          : 1

            x: centreX - width / 2
            y: bottomY - height
            width: lines.width + 2 * horizontalPadding
            height: lines.height + Ui.hudPx(4)
            visible: hudVisibility > 0
            opacity: labelOpacity * hudVisibility
            scale: labelScale
            transformOrigin: Item.Bottom
            color: Theme.hudBackdrop
            radius: Ui.hudPx(3)

            Column {
                id: lines

                x: worldLabel.horizontalPadding
                y: Ui.hudPx(2)

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(implicitWidth, worldLabel.lineLimit)
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    textFormat: Text.PlainText
                    text: worldLabel.title
                    color: Theme.text
                    font.pixelSize: Ui.hudPx(worldLabel.heading ? 24 : 14)
                    font.bold: worldLabel.heading
                }

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: worldLabel.detail.length > 0
                    width: Math.min(implicitWidth, worldLabel.lineLimit)
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    textFormat: Text.PlainText
                    text: worldLabel.detail
                    color: Theme.text
                    font.pixelSize: Ui.hudPx(11)
                }
            }
        }
    }
}
