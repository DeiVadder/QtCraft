// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Two to four short choices side by side (decision E-845, docs/UI-Leitfaden.md 6.6), the chosen one
// in the accent; the pad moves between them and confirms, the arrow keys choose the next one.
// SettingChoice puts them into a setting row; the character screen's preview uses them alone.
// `model` lists { text, value }; `activated(value)` follows a choice by the player; `label` names
// the choice for screen readers. With room, all segments are as wide as the widest text needs (in
// bold, as when chosen), so a short choice does not look less important than a long one; with
// less room they shrink towards their own texts' widths, the short ones first (decision E-931).
RowLayout {
    id: choice

    property string label
    property var model: []
    property var currentValue

    // The width of every segment: the widest text with its padding.
    readonly property real segmentWidth: {
        // advanceWidth() is a call: the font (it follows the UI scale) is read for the binding.
        void metrics.font;
        let widest = 0;
        for (const entry of model)
            widest = Math.max(widest, metrics.advanceWidth(entry.text));
        return Math.ceil(widest) + 2 * Ui.space.xs;
    }

    signal activated(var value)

    // Left and right on the keyboard choose the segment beside the one at `index`.
    function step(index: int, direction: int) {
        const target = index + direction;
        if (target < 0 || target >= segments.count)
            return;

        activated(model[target].value);
        segments.itemAt(target).forceActiveFocus(Qt.TabFocusReason);
    }

    // The segments touch; the bevels separate them.
    spacing: 0

    FontMetrics {
        id: metrics

        font.pixelSize: Ui.type.body
        font.bold: true
    }

    Repeater {
        id: segments

        model: choice.model

        delegate: AbstractButton {
            id: segment

            required property var modelData
            required property int index

            Layout.fillWidth: true
            Layout.preferredWidth: choice.segmentWidth
            Layout.minimumWidth: implicitWidth
            implicitHeight: Ui.size.field
            checkable: false
            checked: modelData.value === choice.currentValue
            text: modelData.text
            Accessible.role: Accessible.RadioButton
            Accessible.name: choice.label.length > 0 ? qsTr("%1: %2").arg(choice.label).arg(text) : text
            Accessible.checked: checked
            // The focus outline reaches over the neighbours.
            z: visualFocus ? 1 : 0
            onClicked: choice.activated(modelData.value)
            Keys.onLeftPressed: choice.step(index, -1)
            Keys.onRightPressed: choice.step(index, 1)

            background: Rectangle {
                color: segment.checked ? Theme.accent : segment.hovered ? Theme.buttonHover : Theme.field

                Bevel {
                    sunken: !segment.checked
                }
                FocusOutline {
                    visible: segment.visualFocus
                }
            }
            contentItem: Label {
                leftPadding: Ui.space.xs
                rightPadding: Ui.space.xs
                text: segment.text
                font.pixelSize: Ui.type.body
                font.bold: segment.checked
                color: segment.checked ? Theme.accentText : Theme.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }
}
