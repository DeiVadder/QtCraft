// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtCraft

// A status row of item icons that stand for points (PlayerController.armourRow, foodRow): each
// icon pale beneath and covered by its share of the points (`fill`). The icons are pixel art:
// drawn at their source size and enlarged with the HUD scale without smoothing (smooth: false), as
// the slots draw their 32 px icons.
Row {
    id: iconRow

    // Icons exist once the content arrived; the provider is asked again then.
    required property bool iconsReady
    required property PointsRow pointsRow
    // The item the icons show ("" shows none).
    required property string item
    // The icons' source size in pixels.
    required property int iconSize
    // The row runs from the right and its icons fill from their right side (food).
    property bool fromRight
    readonly property string source: iconsReady && item !== ""
                                     ? ItemIcons.source(item, ItemIcons.generation) : ""

    spacing: Ui.hudPx(2)
    visible: pointsRow.shown
    layoutDirection: fromRight ? Qt.RightToLeft : Qt.LeftToRight

    Repeater {
        model: iconRow.pointsRow

        Item {
            id: icon

            required property real fill

            width: Ui.hudPx(iconRow.iconSize)
            height: Ui.hudPx(iconRow.iconSize)

            Image {
                anchors.fill: parent
                source: iconRow.source
                sourceSize: Qt.size(iconRow.iconSize, iconRow.iconSize)
                smooth: false
                opacity: 0.3
            }

            Item {
                anchors.right: iconRow.fromRight ? parent.right : undefined
                width: parent.width * icon.fill
                height: parent.height
                clip: true

                Image {
                    anchors.right: iconRow.fromRight ? parent.right : undefined
                    width: icon.width
                    height: icon.height
                    source: iconRow.source
                    sourceSize: Qt.size(iconRow.iconSize, iconRow.iconSize)
                    smooth: false
                }
            }
        }
    }
}
