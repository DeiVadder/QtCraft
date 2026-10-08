pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

Item {
    id: hud

    required property GameController game

    Label {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        color: "white"
        style: Text.Outline
        styleColor: "#80000000"
        font.pixelSize: 14
        visible: hud.game.showDebug
        text: hud.game.debugText
    }

    Item {
        id: crosshair

        anchors.centerIn: parent
        width: 18
        height: 18

        Rectangle {
            anchors.centerIn: parent
            width: parent.width
            height: 2
            color: "#e0ffffff"
        }

        Rectangle {
            anchors.centerIn: parent
            width: 2
            height: parent.height
            color: "#e0ffffff"
        }
    }

    // Mining progress below the crosshair.
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: crosshair.bottom
        anchors.topMargin: 12
        visible: hud.game.miningProgress > 0
        width: 80
        height: 6
        color: "#80000000"

        Rectangle {
            width: parent.width * Math.min(1, hud.game.miningProgress)
            height: parent.height
            color: "white"
        }
    }

    Label {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 12
        visible: text.length > 0
        text: hud.game.targetLabel
        color: "white"
        style: Text.Outline
        styleColor: "#a0000000"
        font.pixelSize: 16
    }

    Column {
        anchors.left: parent.left
        anchors.bottom: hotbar.top
        anchors.margins: 12
        spacing: 2

        Repeater {
            model: hud.game.chatLines

            Label {
                required property string modelData

                text: modelData
                color: "white"
                style: Text.Outline
                styleColor: "#a0000000"
                font.pixelSize: 15
            }
        }
    }

    // Ten hearts, two health points each; a heart is half full for an odd remainder.
    Row {
        anchors.left: hotbar.left
        anchors.bottom: hotbar.top
        anchors.bottomMargin: 6
        spacing: 2

        Repeater {
            model: Math.ceil(hud.game.maxHealth / 2)

            Item {
                id: heart

                required property int index
                readonly property real fill: Math.max(0, Math.min(1, (hud.game.health - index * 2) / 2))

                width: 22
                height: 22

                Text {
                    anchors.fill: parent
                    text: "\u2665"
                    color: "#60000000"
                    font.pixelSize: 22
                    horizontalAlignment: Text.AlignHCenter
                }

                Item {
                    width: parent.width * heart.fill
                    height: parent.height
                    clip: true

                    Text {
                        width: heart.width
                        height: heart.height
                        text: "\u2665"
                        color: "#e02828"
                        style: Text.Outline
                        styleColor: "#80000000"
                        font.pixelSize: 22
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }

    // Ten food icons, two food points each, emptying from the left like the hearts fill from it.
    Row {
        objectName: "foodBar"
        anchors.right: hotbar.right
        anchors.bottom: hotbar.top
        anchors.bottomMargin: 6
        spacing: 2
        layoutDirection: Qt.RightToLeft

        Repeater {
            model: Math.ceil(hud.game.maxFood / 2)

            Item {
                id: drumstick

                required property int index
                readonly property real fill: Math.max(0, Math.min(1, (hud.game.food - index * 2) / 2))

                width: 22
                height: 22

                Image {
                    anchors.fill: parent
                    // Icons exist once the content arrived; the provider is asked again then.
                    source: hud.game.phase === "playing" ? "image://items/qtcraft:cooked_porkchop" : ""
                    cache: false
                    sourceSize: Qt.size(22, 22)
                    smooth: false
                    opacity: 0.3
                }

                Item {
                    anchors.right: parent.right
                    width: parent.width * drumstick.fill
                    height: parent.height
                    clip: true

                    Image {
                        anchors.right: parent.right
                        width: drumstick.width
                        height: drumstick.height
                        source: hud.game.phase === "playing" ? "image://items/qtcraft:cooked_porkchop" : ""
                        cache: false
                        sourceSize: Qt.size(22, 22)
                        smooth: false
                    }
                }
            }
        }
    }

    Label {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        color: "white"
        style: Text.Outline
        styleColor: "#80000000"
        font.pixelSize: 14
        text: hud.game.timeOfDay
    }

    Row {
        id: hotbar

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: hint.top
        anchors.bottomMargin: 8
        spacing: 2

        // The first nine inventory rows are the hotbar.
        Repeater {
            model: SlotRange {
                source: hud.game.inventory
                count: 9
            }

            ItemSlot {
                required property int slot
                required property string icon
                required property int count
                required property real durability
                required property bool empty

                slotIcon: icon
                slotCount: count
                slotDurability: durability
                slotEmpty: empty
                selected: slot === hud.game.inventory.selectedSlot
            }
        }
    }

    Label {
        id: hint

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 6
        color: "white"
        style: Text.Outline
        styleColor: "#a0000000"
        font.pixelSize: 13
        text: hud.game.mouseCaptured
              ? qsTr("Left: mine/attack  F: mine  Right/R: place  Q: drop  1-9/wheel: select  E: inventory  T: chat  Esc: menu")
              : qsTr("Click to look around. WASD to move, Space to jump, Shift to sneak.")
    }
}
