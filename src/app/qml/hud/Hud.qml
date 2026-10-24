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
    readonly property list<Item> statusRows: [hotbar, experienceBar, experienceLevel, hearts, foodBar, armourBar]
    readonly property real statusTop: Math.min(hotbar.y, experienceBar.visible ? experienceBar.y : hotbar.y, experienceLevel.visible ? experienceLevel.y : hotbar.y, hearts.visible ? hearts.y : hotbar.y, foodBar.visible ? foodBar.y : hotbar.y, armourBar.visible ? armourBar.y : hotbar.y)
    // The status rows, the hotbar and the hint below it: world labels fade out before they reach it.
    readonly property rect statusArea: Qt.rect(hotbar.x, statusTop, hotbar.width, height - statusTop)

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
        visible: hud.game.settings.showLabels
        labels: hud.game.hud.labels
        hudArea: hud.statusArea
    }

    // Light values around the target (debug options).
    WorldLabels {
        anchors.fill: parent
        visible: hud.game.settings.debug.lightValues
        labels: hud.game.hud.debug.lightLabels
        hudArea: hud.statusArea
    }

    DebugOverlay {
        anchors.fill: parent
        game: hud.game
    }

    // A held filled map (decision E-805), under the hotbar and the status rows; hidden while a
    // screen is open (only the chat line leaves the view free).
    MapInHand {
        heldMap: hud.game.hud.heldMap
        visible: heldMap.held && (hud.game.overlay === GameController.Overlay.None || hud.game.overlay === GameController.Overlay.Chat)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
    }

    Item {
        id: crosshair

        anchors.centerIn: parent
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
        visible: hud.game.hud.miningProgress > 0
        width: Ui.hudPx(80)
        height: Ui.hudPx(6)
        color: Theme.hudBackdrop

        Rectangle {
            width: parent.width * Math.min(1, hud.game.hud.miningProgress)
            height: parent.height
            color: Theme.progress
        }
    }

    HudLabel {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Ui.hudPx(12)
        visible: text.length > 0
        text: hud.game.hud.targetLabel
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
                model: hud.game.hud.chat.entries

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
    // (decision E-234). The icons are pixel art: drawn at their 20 px source size and enlarged
    // with the HUD scale without smoothing (smooth: false), as the slots draw their 32 px icons.
    Row {
        id: armourBar

        objectName: "armourBar"
        anchors.left: hotbar.left
        anchors.bottom: hearts.top
        anchors.bottomMargin: Ui.hudPx(2)
        spacing: Ui.hudPx(2)
        visible: hud.game.player.armour > 0 && !hud.game.player.creative && !hud.game.player.spectator

        Repeater {
            model: Math.ceil(hud.game.player.maxArmour / 2)

            Item {
                id: plate

                required property int index
                readonly property real fill: Math.max(0, Math.min(1, (hud.game.player.armour - index * 2) / 2))

                width: Ui.hudPx(20)
                height: Ui.hudPx(20)

                Image {
                    anchors.fill: parent
                    source: hud.game.phase === GameController.Phase.Playing && hud.game.player.armourIcon !== "" ? ItemIcons.source(hud.game.player.armourIcon, ItemIcons.generation) : ""
                    sourceSize: Qt.size(20, 20)
                    smooth: false
                    opacity: 0.3
                }

                Item {
                    width: parent.width * plate.fill
                    height: parent.height
                    clip: true

                    Image {
                        width: plate.width
                        height: plate.height
                        source: hud.game.phase === GameController.Phase.Playing && hud.game.player.armourIcon !== "" ? ItemIcons.source(hud.game.player.armourIcon, ItemIcons.generation) : ""
                        sourceSize: Qt.size(20, 20)
                        smooth: false
                    }
                }
            }
        }
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
        visible: !hud.game.player.creative && !hud.game.player.spectator
        color: Theme.slotBorder
        border.color: Theme.textOutline
        border.width: 1

        Rectangle {
            x: 1
            y: 1
            width: (parent.width - 2) * Math.max(0, Math.min(1, hud.game.player.experienceProgress))
            height: parent.height - 2
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
        visible: !hud.game.player.creative && !hud.game.player.spectator

        Repeater {
            model: Math.ceil(hud.game.player.maxHealth / 2)

            Item {
                id: heart

                required property int index
                readonly property real fill: Math.max(0, Math.min(1, (hud.game.player.health - index * 2) / 2))

                width: Ui.hudPx(22)
                height: Ui.hudPx(22)

                Text {
                    anchors.fill: parent
                    text: "\u2665"
                    color: Theme.slotBorder
                    font.pixelSize: Ui.hudPx(22)
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
                        color: hud.game.player.heartColour !== "" ? hud.game.player.heartColour : Theme.health
                        style: Text.Outline
                        styleColor: Theme.textOutline
                        font.pixelSize: Ui.hudPx(22)
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }

    // Ten food icons, two food points each, emptying from the left like the hearts fill from it.
    // Pixel art like the armour icons: 22 px sources enlarged without smoothing.
    Row {
        id: foodBar

        objectName: "foodBar"
        anchors.right: hotbar.right
        anchors.bottom: experienceLevel.top
        anchors.bottomMargin: 1
        spacing: Ui.hudPx(2)
        visible: !hud.game.player.creative && !hud.game.player.spectator
        layoutDirection: Qt.RightToLeft

        Repeater {
            model: Math.ceil(hud.game.player.maxFood / 2)

            Item {
                id: drumstick

                required property int index
                readonly property real fill: Math.max(0, Math.min(1, (hud.game.player.food - index * 2) / 2))

                width: Ui.hudPx(22)
                height: Ui.hudPx(22)

                Image {
                    anchors.fill: parent
                    // Icons exist once the content arrived; the provider is asked again then.
                    source: hud.game.phase === GameController.Phase.Playing ? ItemIcons.source("qtcraft:cooked_porkchop", ItemIcons.generation) : ""
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
                        source: hud.game.phase === GameController.Phase.Playing ? ItemIcons.source("qtcraft:cooked_porkchop", ItemIcons.generation) : ""
                        sourceSize: Qt.size(22, 22)
                        smooth: false
                    }
                }
            }
        }
    }

    HudLabel {
        id: timeOfDay

        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Ui.hudPx(8)
        text: hud.game.hud.timeOfDay
    }

    // Status effects under the time of day (decision E-450): the icon of each, its name with the
    // level in Roman numerals (from II on) and the time left.
    Column {
        objectName: "statusEffects"

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
                    text: (effectRow.modelData.level > 1 ? effectRow.modelData.label + " " + effectRow.modelData.levelName : effectRow.modelData.label)
                          + "  " + Math.floor(effectRow.modelData.seconds / 60) + ":" + String(effectRow.modelData.seconds % 60).padStart(2, "0")
                }
            }
        }
    }

    Row {
        id: hotbar

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
            objectName: "heldName"
            anchors.left: hotbar.right
            anchors.leftMargin: Ui.hudPx(4)
            anchors.bottom: hotbar.top
            visible: customName !== "" && hotbar.visible
            text: customName
            font.italic: true
        }
    }

    // A held compass or clock (decision E-769) right of the hotbar.
    DialDisplay {
        hudState: hud.game.hud
        anchors.left: hotbar.right
        anchors.bottom: hotbar.bottom
        anchors.leftMargin: Ui.hudPx(12)
    }

    HudLabel {
        id: hint

        readonly property var keys: hud.game.settings.keys.labels

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Ui.hudPx(6)
        // Wraps rather than reaching past the window's edges at a large scale.
        width: Math.min(implicitWidth, hud.width - Ui.hudPx(24))
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        // The pad's hints of a screen take this place (PadHints); the pad's world hints lie over it
        // (padHint), so the rows above keep their place.
        visible: !hud.game.gamepad.focusShown
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

        objectName: "hudPadHint"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: hint.verticalCenter
        active: hud.game.gamepad.active && !hud.game.gamepad.focusShown
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
