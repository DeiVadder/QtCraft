// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The beacon's screen (SlotScreen, decision E-1142), laid out like the reference's: the primary
// effects by tier on the left, the secondary ones on the right (from a level-4 pyramid), the
// payment slot with what it takes and the button that pays below them, and the inventory below.
// What the player picks stays on the screen until paid for; the server checks the choice against
// the beacon's level and sends the beacon's state back.
SlotScreen {
    id: screen

    readonly property var beacon: game.beacon

    title: qsTr("Beacon")
    handbookTarget: screen.beacon.handbookTarget

    Label {
        Layout.alignment: Qt.AlignHCenter
        text: screen.beacon.level > 0 ? qsTr("Pyramid level %1 of %2").arg(screen.beacon.level).arg(screen.beacon.maxLevel) : qsTr("No pyramid or no sky: the beacon is dark")
        color: Theme.textSecondary
        font.pixelSize: Ui.type.help
    }

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: Ui.space.l

        ColumnLayout {
            spacing: Ui.space.xs

            Label {
                text: qsTr("Primary power")
                color: Theme.heading
            }

            Flow {
                Layout.preferredWidth: Ui.size.slotCell * 2 + Ui.space.xs
                spacing: Ui.space.xs

                Repeater {
                    model: screen.beacon.primaryOptions

                    BeaconEffectButton {
                        required property var modelData
                        required property int index
                        objectName: "beaconPrimary" + index
                        option: modelData
                        chosen: screen.beacon.primary === modelData.name
                        onClicked: screen.beacon.choosePrimary(modelData.name)
                    }
                }
            }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            spacing: Ui.space.xs

            Label {
                text: qsTr("Secondary power")
                color: Theme.heading
            }

            Row {
                spacing: Ui.space.xs

                Repeater {
                    model: screen.beacon.secondaryOptions

                    BeaconEffectButton {
                        required property var modelData
                        required property int index
                        objectName: "beaconSecondary" + index
                        option: modelData
                        chosen: screen.beacon.secondary !== "" && screen.beacon.secondary === modelData.name
                        onClicked: screen.beacon.chooseSecondary(modelData.name)
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: Ui.space.m

        MachineSlotRow {
            owner: screen
            first: InventoryModel.MachineFirst
            names: ["beaconPayment"]
        }

        Label {
            Layout.alignment: Qt.AlignVCenter
            text: screen.beacon.paymentItems
            color: Theme.textMuted
            font.pixelSize: Ui.type.help
        }

        GameButton {
            Layout.alignment: Qt.AlignVCenter
            objectName: "beaconConfirm"
            enabled: screen.beacon.canConfirm
            text: qsTr("Confirm")
            onClicked: screen.beacon.confirm()
        }
    }
}
