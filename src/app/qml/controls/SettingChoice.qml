pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// A setting with two to four short choices side by side (decision E-845, docs/UI-Leitfaden.md
// 6.6): segments show every choice at once, the chosen one in the accent; the pad moves between
// them and confirms, the arrow keys choose the next one. Five or more choices, or long ones, are a
// SettingDropdown. `model` lists { text, value }; `activated(value)` follows a choice by the player.
SettingRow {
    id: setting

    property var model: []
    property var currentValue

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
    RowLayout {
        Layout.fillWidth: true
        spacing: 0

        Repeater {
            id: segments

            model: setting.model

            delegate: AbstractButton {
                id: segment

                required property var modelData
                required property int index

                Layout.fillWidth: true
                implicitHeight: Ui.size.field
                checkable: false
                checked: modelData.value === setting.currentValue
                text: modelData.text
                Accessible.role: Accessible.RadioButton
                Accessible.name: qsTr("%1: %2").arg(setting.label).arg(text)
                Accessible.checked: checked
                // The focus outline reaches over the neighbours.
                z: visualFocus ? 1 : 0
                onClicked: setting.activated(modelData.value)
                Keys.onLeftPressed: setting.step(index, -1)
                Keys.onRightPressed: setting.step(index, 1)

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
}
