// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtCraft

// One effect a beacon offers (decision E-1142): the effect's icon on a field of its colour, a frame
// when chosen, "II" for the primary effect at level II. A click, Space or Enter chooses it; one the
// beacon's level does not reach is greyed out. A screen reader hears its name and whether it is
// chosen or locked.
AbstractButton {
    id: button

    // One map of BeaconController.primaryOptions or secondaryOptions.
    required property var option
    required property bool chosen

    enabled: option.available
    checkable: false
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: Ui.size.slotCell
    implicitHeight: Ui.size.slotCell
    opacity: enabled ? 1.0 : 0.4
    Accessible.role: Accessible.RadioButton
    Accessible.checked: chosen
    Accessible.name: option.levelTwo ? qsTr("%1 II").arg(option.label) : option.label
    Accessible.description: enabled ? "" : qsTr("needs a pyramid of level %1").arg(option.tier)
    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()

    background: Rectangle {
        radius: Ui.radius
        color: button.hovered ? Theme.slotHover : Theme.slot
        border.color: button.chosen || button.visualFocus ? Theme.selection : Theme.slotBorder
        border.width: button.chosen ? Ui.size.bar : Ui.size.line
    }

    ThemedToolTip {
        shown: button.hovered
        text: button.Accessible.name
    }

    contentItem: Item {
        Rectangle {
            anchors.centerIn: parent
            width: Ui.size.icon * 2
            height: Ui.size.icon * 2
            radius: Ui.radius
            color: Qt.alpha(button.option.colour, 0.6)
            border.color: button.option.colour

            Image {
                anchors.fill: parent
                anchors.margins: Ui.size.bar
                visible: button.option.icon !== ""
                source: visible ? ItemIcons.source(button.option.icon, ItemIcons.generation) : ""
                smooth: false
            }
        }

        Label {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: button.option.levelTwo === true
            text: "II"
            color: Theme.text
            font.pixelSize: Ui.type.help
            font.bold: true
        }
    }
}
