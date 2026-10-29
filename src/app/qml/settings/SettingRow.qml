// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// One setting (decision E-845, docs/UI-Leitfaden.md 6.3): the label with an optional help line and
// badge ("from the next world start") on the left 40 %, the control on the right 60 %, at least a
// row high, so the controls stand at the same place on every page. The key of the action, if it has
// one, stands as a plate next to the label (KeyBadge). Disabled, the row dims and its
// help says why; `dimmed` dims it while it stays usable (the volumes while muted). The control goes
// in as the row's child; SettingSlider and its siblings bring theirs.
RowLayout {
    id: row

    property string label
    property string help
    property string badge
    // The key that does the same in the world ("L"); empty for none.
    property string key
    // Dims the row while its control stays usable.
    property bool dimmed: false
    default property alias control: slot.data

    Layout.fillWidth: true
    Layout.minimumHeight: Ui.size.row
    spacing: Ui.space.m
    opacity: enabled && !dimmed ? 1 : Ui.disabledOpacity

    // The columns share the width 2 : 3 whatever their texts (preferred width 0, stretch factors).
    ColumnLayout {
        Layout.fillWidth: true
        Layout.preferredWidth: 0
        Layout.horizontalStretchFactor: 2
        Layout.alignment: Qt.AlignVCenter
        spacing: Ui.space.xs

        RowLayout {
            Layout.fillWidth: true
            spacing: Ui.space.s

            Label {
                Layout.fillWidth: true
                text: row.label
                font.pixelSize: Ui.type.body
                color: Theme.text
                wrapMode: Text.Wrap
            }
            KeyBadge {
                visible: row.key.length > 0
                text: row.key
            }
        }
        // A note when the setting takes effect ("from the next world start").
        Rectangle {
            Layout.alignment: Qt.AlignLeft
            visible: row.badge.length > 0
            implicitWidth: badgeLabel.implicitWidth + 2 * Ui.space.xs
            implicitHeight: Ui.size.badge
            radius: Ui.radius
            color: Theme.field

            Label {
                id: badgeLabel

                anchors.centerIn: parent
                text: row.badge
                font.pixelSize: Ui.type.badge
                font.bold: true
                color: Theme.heading
            }
        }
        Label {
            Layout.fillWidth: true
            visible: row.help.length > 0
            text: row.help
            font.pixelSize: Ui.type.help
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
    }

    ControlSlot {
        id: slot

        dimsControls: row.opacity < 1
        Layout.fillWidth: true
        Layout.preferredWidth: 0
        Layout.horizontalStretchFactor: 3
        Layout.alignment: Qt.AlignVCenter
        spacing: Ui.space.m
    }
}
