// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The players on the server while the player list key is held (decision E-259): the server's name
// and message of the day, then every player with an operator mark and the round trip of their client
// in milliseconds and signal bars (decision E-886); the own entry is highlighted, spectators come
// last and greyed (decision E-730).
// It takes no input, the world keeps moving.
Item {
    id: overlay

    required property GameController game

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Ui.hudPx(48)
        width: Math.min(parent.width - Ui.hudPx(64), Ui.hudPx(420))
        height: content.implicitHeight + Ui.hudPx(24)
        radius: Ui.hudPx(6)
        color: Theme.hudBackdrop
        border.color: Theme.panelBorder

        ColumnLayout {
            id: content

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Ui.hudPx(12)
            spacing: Ui.hudPx(4)

            Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: overlay.game.multiplayer.players.serverName
                color: Theme.heading
                font.pixelSize: Ui.hudPx(20)
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                textFormat: Text.PlainText
                text: overlay.game.multiplayer.players.motd
                color: Theme.textSecondary
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("%n player(s) online", "", overlay.game.multiplayer.players.count)
                color: Theme.textSecondary
                horizontalAlignment: Text.AlignHCenter
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.separator
            }

            Repeater {
                model: overlay.game.multiplayer.players.players

                delegate: Rectangle {
                    id: playerRow

                    required property var modelData

                    Layout.fillWidth: true
                    Layout.preferredHeight: Ui.hudPx(26)
                    radius: Ui.hudPx(3)
                    color: playerRow.modelData.self ? Theme.rowHighlight : Theme.clear

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Ui.hudPx(8)
                        anchors.rightMargin: Ui.hudPx(8)
                        spacing: Ui.hudPx(8)

                        Label {
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            text: playerRow.modelData.self ? qsTr("%1 (you)").arg(playerRow.modelData.name) : playerRow.modelData.name
                            // Spectators are greyed and slanted (decision E-730).
                            color: playerRow.modelData.spectator ? Theme.textSecondary : Theme.text
                            font.bold: playerRow.modelData.self
                            font.italic: playerRow.modelData.spectator
                            elide: Text.ElideRight
                        }

                        Label {
                            visible: playerRow.modelData.spectator
                            text: qsTr("Spectator")
                            color: Theme.textSecondary
                            font.pixelSize: Ui.hudPx(12)
                        }

                        Label {
                            visible: playerRow.modelData.operator
                            text: qsTr("Operator")
                            color: Theme.textSecondary
                            font.pixelSize: Ui.hudPx(12)
                        }

                        Label {
                            // No round trip measured (the host plays in the server's process): "–".
                            text: playerRow.modelData.latencyMs < 0 ? "–" : qsTr("%1 ms").arg(playerRow.modelData.latencyText)
                            color: Theme.textSecondary
                            font.pixelSize: Ui.hudPx(12)
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: Ui.hudPx(52)
                        }

                        // Signal bars as in the reference: five below 150 ms, one from a second on.
                        Row {
                            id: bars

                            readonly property int lit: playerRow.modelData.bars
                            readonly property color litColor: lit >= 4 ? Theme.statusGood : lit >= 2 ? Theme.statusWarn : Theme.statusBad

                            Layout.alignment: Qt.AlignVCenter
                            spacing: Ui.hudPx(1)
                            Accessible.role: Accessible.Indicator
                            Accessible.name: qsTr("%n of 5 signal bars", "", bars.lit)

                            Repeater {
                                model: 5

                                delegate: Rectangle {
                                    required property int index

                                    // Standing on a common base line: the Row places x only.
                                    y: Ui.hudPx(12) - height
                                    width: Ui.hudPx(3)
                                    height: Ui.hudPx(4 + 2 * index)
                                    color: index < bars.lit ? bars.litColor : Theme.textMuted
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
