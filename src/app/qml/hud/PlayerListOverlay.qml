pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The players on the server while the player list key is held (decision E-259): the server's name
// and message of the day, then every player with an operator mark; the own entry is highlighted.
// It takes no input, the world keeps moving.
Item {
    id: overlay

    required property GameController game

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 48
        width: Math.min(parent.width - 64, 420)
        height: content.implicitHeight + 24
        radius: 6
        color: Theme.hudBackdrop
        border.color: Theme.panelBorder

        ColumnLayout {
            id: content

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            spacing: 4

            Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: overlay.game.multiplayer.players.serverName
                color: Theme.heading
                font.pixelSize: 20
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
                    Layout.preferredHeight: 26
                    radius: 3
                    color: playerRow.modelData.self ? Theme.rowHighlight : Theme.clear

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            text: playerRow.modelData.self ? qsTr("%1 (you)").arg(playerRow.modelData.name) : playerRow.modelData.name
                            color: Theme.text
                            font.bold: playerRow.modelData.self
                            elide: Text.ElideRight
                        }

                        Label {
                            visible: playerRow.modelData.operator
                            text: qsTr("Operator")
                            color: Theme.warning
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
    }
}
