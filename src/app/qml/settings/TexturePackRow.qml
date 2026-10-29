// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// One texture pack on the texture pack page (decision E-297): preview, name and resolution, the
// description, licence and origin, credits, why it cannot be used and the files it skipped; on the
// right the switch that uses it and, while it is active, the arrows that change its priority. Pack
// texts are data from files: never rich text (E-272), ListRow shows them plain.
ListRow {
    id: packRow

    required property var modelData
    required property int index
    required property var packs

    title: modelData.name + (modelData.resolution > 0 ? " " + qsTr("(%1x)").arg(modelData.resolution) : "")
    subtitle: modelData.description

    leading: Image {
        width: Ui.size.previewWidth
        height: Ui.size.previewHeight
        source: packRow.modelData.preview
        sourceSize: Qt.size(Ui.size.previewWidth, Ui.size.previewHeight)
        fillMode: Image.PreserveAspectFit
        smooth: false
    }

    actions: [
        GameSwitch {
            objectName: "texturePackSwitch_" + packRow.modelData.id
            Accessible.name: qsTr("Use %1").arg(packRow.modelData.name)
            enabled: packRow.modelData.error.length === 0
            checked: packRow.modelData.active
            onToggled: packRow.packs.setActive(packRow.modelData.id, checked)
        },
        RowLayout {
            visible: packRow.modelData.active
            spacing: Ui.space.xs

            GameButton {
                text: "▲"
                Accessible.name: qsTr("Higher priority")
                enabled: packRow.index > 0
                onClicked: packRow.packs.move(packRow.modelData.id, -1)

                ThemedToolTip {
                    shown: parent.hovered
                    text: qsTr("Higher priority")
                }
            }
            GameButton {
                text: "▼"
                Accessible.name: qsTr("Lower priority")
                enabled: packRow.index + 1 < packRow.packs.packs.length && packRow.packs.packs[packRow.index + 1].active
                onClicked: packRow.packs.move(packRow.modelData.id, 1)

                ThemedToolTip {
                    shown: parent.hovered
                    text: qsTr("Lower priority")
                }
            }
        }
    ]

    Label {
        textFormat: Text.PlainText
        Layout.fillWidth: true
        text: (packRow.modelData.license.length > 0 ? qsTr("Licence: %1").arg(packRow.modelData.license) + " · " : "") + (packRow.modelData.origin === "builtin" ? qsTr("Built in") : packRow.modelData.origin === "mod" ? qsTr("From a mod") : qsTr("Own pack"))
        font.pixelSize: Ui.type.help
        color: Theme.textMuted
        wrapMode: Text.Wrap
    }
    Label {
        textFormat: Text.PlainText
        Layout.fillWidth: true
        visible: packRow.modelData.credits.length > 0
        text: qsTr("Credits: %1").arg(packRow.modelData.credits)
        font.pixelSize: Ui.type.help
        color: Theme.textMuted
        wrapMode: Text.Wrap
    }
    Label {
        textFormat: Text.PlainText
        Layout.fillWidth: true
        visible: packRow.modelData.error.length > 0
        text: qsTr("Cannot be used: %1").arg(packRow.modelData.error)
        color: Theme.error
        wrapMode: Text.Wrap
    }
    Label {
        textFormat: Text.PlainText
        Layout.fillWidth: true
        visible: packRow.modelData.problems.length > 0
        text: packRow.modelData.problems.length > 0 ? qsTr("%n file(s) skipped or changed:", "", packRow.modelData.problems.length) + "\n" + packRow.modelData.problems.join("\n") : ""
        color: Theme.warning
        wrapMode: Text.Wrap
        maximumLineCount: 8
        elide: Text.ElideRight
    }
}
