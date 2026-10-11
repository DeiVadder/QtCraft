pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtCraft

// Labels floating in the world (the names in the showcase world, the debug display's light values):
// drawn where the game puts them on the view every frame, nearer ones larger, fading out with
// distance; the label key switches the names.
// A label is no wider than the space to its neighbour (labelMaxWidth): longer lines end in "…",
// the detail line is smaller than the name.
Item {
    id: layer

    // A QcWorldLabelModel.
    required property var labels

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

            readonly property real horizontalPadding: 6
            readonly property real lineLimit: heading ? Number.POSITIVE_INFINITY
                                                      : Math.max(32, labelMaxWidth - 2 * horizontalPadding)

            x: labelX - width / 2
            y: labelY - height
            width: lines.width + 2 * horizontalPadding
            height: lines.height + 4
            opacity: labelOpacity
            scale: labelScale
            transformOrigin: Item.Bottom
            color: Theme.hudBackdrop
            radius: 3

            Column {
                id: lines

                x: worldLabel.horizontalPadding
                y: 2

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(implicitWidth, worldLabel.lineLimit)
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    textFormat: Text.PlainText
                    text: worldLabel.title
                    color: Theme.text
                    font.pixelSize: worldLabel.heading ? 24 : 14
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
                    font.pixelSize: 11
                }
            }
        }
    }
}
