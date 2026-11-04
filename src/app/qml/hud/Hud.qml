// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

Item {
    id: hud

    required property GameController game
    // The rows over the hotbar, bottom to top: the chat lines end above the highest one shown, so
    // they never cover a row at any window size. A new row over the hotbar belongs in this list and
    // in statusTop; the layout check of screenshot runs (QcLayoutCheck) reports chat lines that
    // overlap a row of the list.
    readonly property list<Item> statusRows: [hotbar, experienceBar, experienceLevel, hearts, foodBar, armourBar, airBar]
    readonly property real statusTop: Math.min(hotbar.y, experienceBar.visible ? experienceBar.y : hotbar.y, experienceLevel.visible ? experienceLevel.y : hotbar.y, hearts.visible ? hearts.y : hotbar.y, foodBar.visible ? foodBar.y : hotbar.y, armourBar.visible ? armourBar.y : hotbar.y, airBar.visible ? airBar.y : hotbar.y)
    // The status rows, the hotbar and the hint below it: world labels fade out before they reach it.
    readonly property rect statusArea: Qt.rect(hotbar.x, statusTop, hotbar.width, height - statusTop)
    // What shows behind the open screen (decision E-927): the view's parts only while the view is
    // free, the world hint only without any screen (HudState.viewShown, hintShown). Main.qml hides
    // the whole HUD behind a full-screen screen.
    readonly property HudState hudState: game.hud
    // Item icons exist once the world plays (the content arrived).
    readonly property bool playing: game.phase === GameController.Phase.Playing

    objectName: "hud"

    // On fire (decision E-393): flames rise from the bottom of the view and flicker until the
    // burning ends or water puts it out.
    Rectangle {
        id: burningOverlay

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * 0.4
        visible: hud.game.player.burning
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: Qt.alpha(Theme.flame, 0.0)
            }
            GradientStop {
                position: 0.6
                color: Qt.alpha(Theme.flame, 0.6)
            }
            GradientStop {
                position: 1.0
                color: Theme.flameCore
            }
        }

        SequentialAnimation on opacity {
            running: burningOverlay.visible
            loops: Animation.Infinite

            NumberAnimation {
                to: 0.7
                duration: 180
            }
            NumberAnimation {
                to: 0.45
                duration: 240
            }
        }
    }

    WorldLabels {
        anchors.fill: parent
        visible: hud.game.settings.showLabels && hud.hudState.viewShown
        labels: hud.hudState.labels
        hudArea: hud.statusArea
    }

    // Light values around the target (debug options).
    WorldLabels {
        anchors.fill: parent
        visible: hud.game.settings.debug.lightValues && hud.hudState.viewShown
        labels: hud.hudState.debug.lightLabels
        hudArea: hud.statusArea
    }

    DebugOverlay {
        anchors.fill: parent
        game: hud.game
    }

    // A held filled map (decision E-805), under the hotbar and the status rows; hidden while a
    // screen is open (only the chat line leaves the view free).
    MapInHand {
        heldMap: hud.hudState.heldMap
        visible: heldMap.held && hud.hudState.viewShown
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
    }

    Item {
        id: crosshair

        objectName: "hudCrosshair"
        anchors.centerIn: parent
        visible: hud.hudState.viewShown
        width: Ui.hudPx(18)
        height: Ui.hudPx(18)

        Rectangle {
            anchors.centerIn: parent
            width: parent.width
            height: Ui.hudPx(2)
            color: Theme.crosshair
            border.color: Theme.textOutline
            border.width: 0.5
        }

        Rectangle {
            anchors.centerIn: parent
            width: Ui.hudPx(2)
            height: parent.height
            color: Theme.crosshair
            border.color: Theme.textOutline
            border.width: 0.5
        }
    }

    // Mining progress below the crosshair.
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: crosshair.bottom
        anchors.topMargin: Ui.hudPx(12)
        visible: hud.hudState.miningProgress > 0
        width: Ui.hudPx(80)
        height: Ui.hudPx(6)
        color: Theme.hudBackdrop

        Rectangle {
            width: parent.width * Math.min(1, hud.hudState.miningProgress)
            height: parent.height
            color: Theme.progress
        }
    }

    // The name of the block in view.
    HudLabel {
        objectName: "hudTarget"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Ui.hudPx(12)
        visible: text.length > 0 && hud.hudState.viewShown
        text: hud.hudState.targetLabel
        font.pixelSize: Ui.hudPx(16)
    }

    // The chat lines above the status rows (decision E-875): closed, the last lines fade out after
    // ten seconds; open, the last twenty show again. Long lines wrap at a width that grows with the
    // HUD scale, so a long list (/help) does not run across a wide window. The area never reaches
    // above the window's top: there the oldest lines of a long open chat are cut off. A fading
    // line animates its own opacity over the time the feed gives it (`fadeMs`), so the list is
    // only made again when a line comes, starts to fade or goes.
    Item {
        id: chatArea

        objectName: "hudChat"
        visible: hud.hudState.viewShown
        x: Ui.hudPx(12)
        width: Math.min(hud.width - 2 * x, Ui.hudPx(480))
        height: Math.min(chat.height, Math.max(0, hud.statusTop - Ui.hudPx(6) - Ui.hudPx(12)))
        y: hud.statusTop - Ui.hudPx(6) - height
        clip: true

        Column {
            id: chat

            anchors.bottom: parent.bottom
            width: parent.width
            spacing: Ui.hudPx(2)

            Repeater {
                model: hud.hudState.chat.entries

                HudLabel {
                    id: chatLine

                    required property var modelData

                    width: Math.min(implicitWidth, chat.width)
                    wrapMode: Text.Wrap
                    text: modelData.text
                    opacity: modelData.opacity
                    font.pixelSize: Ui.hudPx(15)

                    NumberAnimation on opacity {
                        running: chatLine.modelData.fadeMs > 0
                        to: 0
                        duration: chatLine.modelData.fadeMs
                    }
                }
            }
        }
    }

    // Armour points above the hearts while something is worn: ten chestplates, two points each
    // (decision E-234), drawn at their 20 px source size.
    PointsIconRow {
        id: armourBar

        objectName: "armourBar"
        anchors.left: hotbar.left
        // Above the extra hearts of absorption; their empty row has no height.
        anchors.bottom: absorptionHearts.top
        anchors.bottomMargin: Ui.hudPx(2)
        iconsReady: hud.playing
        pointsRow: hud.game.player.armourRow
        item: hud.game.player.armourIcon
        iconSize: 20
    }

    // The experience bar over the hotbar with the level centred just above it (decisions E-398,
    // E-399); creative players gather experience too but see no bar, as in the reference.
    Rectangle {
        id: experienceBar

        objectName: "experienceBar"
        anchors.left: hotbar.left
        anchors.right: hotbar.right
        anchors.bottom: hotbar.top
        anchors.bottomMargin: Ui.hudPx(4)
        height: Ui.hudPx(7)
        visible: hud.game.player.statusShown
        color: Theme.slotBorder
        border.color: Theme.textOutline
        border.width: 1

        // Inside the border on both sides.
        Rectangle {
            x: parent.border.width
            y: parent.border.width
            width: (parent.width - 2 * x) * Math.max(0, Math.min(1, hud.game.player.experienceProgress))
            height: parent.height - 2 * y
            color: Theme.experience
        }
    }

    // The level in a line of its own between the bar and the hearts and food above it, so it never
    // touches them; the line keeps its height when the level is 0 and the number is hidden.
    Text {
        id: experienceLevel

        objectName: "experienceLevel"
        anchors.horizontalCenter: experienceBar.horizontalCenter
        anchors.bottom: experienceBar.top
        anchors.bottomMargin: 1
        visible: experienceBar.visible && hud.game.player.experienceLevel > 0
        text: hud.game.player.experienceLevel
        color: Theme.experience
        style: Text.Outline
        styleColor: Theme.textOutline
        font.pixelSize: Ui.hudPx(15)
        font.bold: true
    }

    // Ten hearts, two health points each; a heart is half full for an odd remainder. Creative
    // players neither lose health nor get hungry: no hearts, no food.
    Row {
        id: hearts

        objectName: "hearts"

        anchors.left: hotbar.left
        anchors.bottom: experienceLevel.top
        anchors.bottomMargin: 1
        spacing: Ui.hudPx(2)
        visible: hud.game.player.heartRow.shown

        Repeater {
            model: hud.game.player.heartRow

            HeartIcon {
                required property real fill

                filled: fill
                colour: hud.game.player.heartColour !== "" ? hud.game.player.heartColour : Theme.health
            }
        }
    }

    // The extra health of absorption above the hearts (decision E-992), two points a heart; a mod's
    // high level fills the row and no more.
    Row {
        id: absorptionHearts

        readonly property int heartPoints: 2

        anchors.left: hotbar.left
        anchors.bottom: hearts.top
        spacing: Ui.hudPx(2)
        visible: hearts.visible

        Repeater {
            model: Math.min(hud.game.player.heartRow.count, Math.ceil(hud.game.player.absorption / absorptionHearts.heartPoints))

            HeartIcon {
                required property int index

                filled: Math.min(1, hud.game.player.absorption / absorptionHearts.heartPoints - index)
                colour: hud.game.player.absorptionColour
            }
        }
    }

    // Ten food icons, two food points each, emptying from the left like the hearts fill from it;
    // 22 px sources.
    PointsIconRow {
        id: foodBar

        objectName: "foodBar"
        anchors.right: hotbar.right
        anchors.bottom: experienceLevel.top
        anchors.bottomMargin: 1
        iconsReady: hud.playing
        pointsRow: hud.game.player.foodRow
        item: hud.game.player.foodIcon
        iconSize: 22
        fromRight: true
    }

    // Air bubbles above the food while under water (decision E-955), as high as the armour.
    AirBar {
        id: airBar

        game: hud.game
        anchors.right: hotbar.right
        anchors.bottom: foodBar.top
        anchors.bottomMargin: Ui.hudPx(2)
    }

    HudLabel {
        id: timeOfDay

        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Ui.hudPx(8)
        text: hud.hudState.timeOfDay
    }

    // Status effects under the time of day (decision E-450): the icon of each, its name with the
    // level in Roman numerals (from II on) and the time left.
    Column {
        anchors.right: parent.right
        anchors.top: timeOfDay.bottom
        anchors.margins: Ui.hudPx(8)
        spacing: Ui.hudPx(4)

        Repeater {
            model: hud.game.player.effects

            Row {
                id: effectRow

                required property var modelData

                anchors.right: parent.right
                spacing: Ui.hudPx(4)

                Rectangle {
                    width: Ui.hudPx(22)
                    height: Ui.hudPx(22)
                    radius: Ui.hudPx(4)
                    color: Qt.alpha(effectRow.modelData.colour, 0.6)
                    border.color: effectRow.modelData.colour

                    Image {
                        anchors.fill: parent
                        anchors.margins: Ui.hudPx(2)
                        visible: effectRow.modelData.icon !== ""
                        source: visible ? ItemIcons.source(effectRow.modelData.icon, ItemIcons.generation) : ""
                        smooth: false
                    }
                }

                HudLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: Ui.hudPx(13)
                    text: effectRow.modelData.text
                }
            }
        }
    }

    Row {
        id: hotbar

        objectName: "hotbar"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: hint.top
        anchors.bottomMargin: Ui.hudPx(8)
        spacing: Ui.hudPx(2)
        // A spectator holds nothing (decision E-730).
        visible: !hud.game.player.spectator

        // The first nine inventory rows are the hotbar.
        Repeater {
            model: SlotRange {
                source: hud.game.player.inventory
                count: 9
            }

            ItemSlot {
                required property int slot
                required property string icon
                required property int count
                required property real durability
                required property bool empty

                unit: Ui.hudScale
                baseColor: Theme.hudSlot
                activeFocusOnTab: false
                slotIcon: icon
                slotCount: count
                slotDurability: durability
                slotEmpty: empty
                selected: slot === hud.game.player.inventory.selectedSlot
                // "Radial menu only" (decision E-614): only the slot in the hand stays, in its place.
                opacity: hud.game.settings.gamepad.radialOnly && !selected ? 0 : 1
            }
        }
    }

    // The custom name of the stack in the hand (decision E-866), in italics right above the end of
    // the hotbar, beside the status rows.
    Repeater {
        model: SlotRange {
            source: hud.game.player.inventory
            first: hud.game.player.inventory.selectedSlot
            count: 1
        }

        HudLabel {
            required property string customName

            anchors.left: hotbar.right
            anchors.leftMargin: Ui.hudPx(4)
            anchors.bottom: hotbar.top
            visible: customName !== "" && hotbar.visible
            text: customName
            font.italic: true
        }
    }

    // The name of the selected item for a moment after it changed (decision E-885), centred over
    // the highest status row above the hotbar (armour, hearts or the hotbar itself in creative mode).
    HeldItemName {
        inventory: hud.game.player.inventory
        // Its own fade, and only while the view is free (decision E-927).
        visible: opacity > 0 && hud.hudState.viewShown
        anchors.horizontalCenter: hotbar.horizontalCenter
        y: Math.min(hotbar.y, hearts.visible ? hearts.y : hotbar.y, armourBar.visible ? armourBar.y : hotbar.y, airBar.visible ? airBar.y : hotbar.y) - height - Ui.hudPx(4)
    }

    // A held compass or clock (decision E-769) right of the hotbar.
    DialDisplay {
        hudState: hud.hudState
        anchors.left: hotbar.right
        anchors.bottom: hotbar.bottom
        anchors.leftMargin: Ui.hudPx(12)
    }

    HudLabel {
        id: hint

        readonly property var keys: hud.game.settings.keys.labels

        objectName: "hudHint"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Ui.hudPx(6)
        // Wraps rather than reaching past the window's edges at a large scale.
        width: Math.min(implicitWidth, hud.width - Ui.hudPx(24))
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        // The pad's hints of a screen take this place (PadHints); the pad's world hints lie over it
        // (padHint), so the rows above keep their place. Hidden, it keeps its height: the hotbar
        // stays where it is.
        visible: !hud.game.gamepad.focusShown && hud.hudState.hintShown
        opacity: hud.game.gamepad.active ? 0 : 1
        font.pixelSize: Ui.hudPx(13)
        text: hud.game.input.mouseCaptured && hud.game.player.spectator
              ? qsTr("Spectator: fly through blocks  %1/%2: up/down  wheel: flight speed ×%3  %4: chat  Esc: menu")
                    .arg(hint.keys.jump).arg(hint.keys.sneak).arg(hud.game.input.flySpeed.toLocaleString(Qt.locale(), "f", 1)).arg(hint.keys.chat)
              : hud.game.input.mouseCaptured
              ? qsTr("Left: mine/attack  %1: mine  Right/%2: place  %3: drop  %4–%5/wheel: select  %6: inventory  %7: chat  Esc: menu")
                    .arg(hint.keys.mine).arg(hint.keys.use).arg(hint.keys.drop).arg(hint.keys.hotbar1).arg(hint.keys.hotbar9).arg(hint.keys.inventory).arg(hint.keys.chat)
              : qsTr("Click to look around. %1%2%3%4 to move, %5 to jump, %6 to sneak.")
                    .arg(hint.keys.forward).arg(hint.keys.left).arg(hint.keys.back).arg(hint.keys.right).arg(hint.keys.jump).arg(hint.keys.sneak)
    }

    // The pad's buttons in the world (decision E-612), as the pad in use labels them and as its
    // profile binds them; made only while the pad leads.
    Loader {
        id: padHint

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: hint.verticalCenter
        active: hud.game.gamepad.active && !hud.game.gamepad.focusShown
        visible: hud.hudState.hintShown
        // Ten buttons in one row: at a large HUD scale the row shrinks to the window's width.
        scale: Math.min(1, (hud.width - Ui.hudPx(16)) / Math.max(1, width))

        sourceComponent: PadHintRow {
            readonly property var actions: hud.game.gamepad.actions

            game: hud.game
            fontSize: Ui.hudPx(12)
            spacing: Ui.hudPx(11)
            entries: [
                { input: actions.mine, text: qsTr("mine/attack") },
                { input: actions.use, text: qsTr("use/place") },
                { input: actions.drop, text: qsTr("drop") },
                { input: actions.pickBlock, text: qsTr("pick block") },
                { input: actions.hotbarPrevious, text: "" },
                { input: actions.hotbarNext, text: qsTr("select") },
                { input: actions.hotbarRadial, text: qsTr("hold: all slots") },
                { input: actions.inventory, text: qsTr("inventory") },
                { input: actions.chat, text: qsTr("chat") },
                { input: actions.pause, text: qsTr("menu") }
            ]
        }
    }
}
