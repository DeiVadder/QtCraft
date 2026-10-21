pragma ComponentBehavior: Bound

import QtQuick
import QtCraft

// A filled map in the hand (decision E-805), large in the lower middle of the view as the reference
// holds it with both hands; the crosshair above stays free. QtCraft has no offhand: with one, a map
// there would be drawn small and to the side instead. Paper first, the map image on it (unexplored
// pixels are transparent, so the paper shows), then the players as pointers (decision E-803) and
// the item frames showing the map in green (decision E-808).
Item {
    id: mapInHand

    required property HeldMap heldMap

    objectName: "mapInHand"
    // Square, about the lower 40 % of the view, never wider than the view.
    width: Math.min(parent ? parent.height * 0.4 : 0, parent ? parent.width * 0.9 : 0)
    height: width

    Rectangle {
        id: paper

        anchors.fill: parent
        color: Theme.mapPaper
        border.color: Theme.mapPaperEdge
        border.width: Math.max(2, Math.round(parent.width / 64))

        Image {
            id: picture

            anchors.fill: parent
            anchors.margins: paper.border.width * 2
            visible: mapInHand.heldMap.known
            source: mapInHand.heldMap.known ? mapInHand.heldMap.source : ""
            sourceSize: Qt.size(128, 128)
            smooth: false
            cache: false

            Repeater {
                model: mapInHand.heldMap.markers

                // A pointer on the map, or a dot at its edge for a player off the map.
                Item {
                    id: marker

                    required property var modelData
                    readonly property real size: picture.width / 16
                    readonly property color tint: marker.modelData.frame ? Theme.mapMarkerFrame : marker.modelData.own ? Theme.mapMarker : Theme.mapMarkerOther

                    x: (marker.modelData.x + 1) / 2 * picture.width - width / 2
                    y: (marker.modelData.z + 1) / 2 * picture.height - height / 2
                    width: marker.size
                    height: marker.size
                    rotation: marker.modelData.onMap ? marker.modelData.rotation : 0

                    // The pointer: a wide body with a narrow tip ahead (north before rotation).
                    Rectangle {
                        visible: marker.modelData.onMap
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: parent.height * 0.4
                        width: parent.width * 0.7
                        height: parent.height * 0.6
                        color: marker.tint
                        border.color: Theme.mapPaperEdge
                        border.width: 1
                    }
                    Rectangle {
                        visible: marker.modelData.onMap
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width * 0.3
                        height: parent.height * 0.5
                        color: marker.tint
                        border.color: Theme.mapPaperEdge
                        border.width: 1
                    }
                    Rectangle {
                        visible: !marker.modelData.onMap
                        anchors.centerIn: parent
                        width: parent.width * 0.5
                        height: width
                        radius: width / 2
                        color: marker.tint
                        border.color: Theme.mapPaperEdge
                        border.width: 1
                    }
                }
            }
        }
    }
}
