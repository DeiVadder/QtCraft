// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

Rectangle {
    id: menu

    required property GameController game
    // The height at the bottom the HUD's status rows take behind the menu (Main.qml); the card stays
    // above them.
    property real statusReserved: 0

    color: Theme.scrim
    focus: visible
    onVisibleChanged: {
        if (visible) {
            forceActiveFocus();
            // Made by its LazyScreen when the overlay changed to one of its sub-screens: opens it.
            followOverlay();
        } else {
            // Its sub-screens belong to the menu: they close (and store the settings) with it.
            settingsScreen.close();
            characterScreen.close();
            lanScreen.close();
            adminScreen.close();
        }
    }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            menu.game.overlay = GameController.Overlay.None;
        } else if (menu.game.settings.keys.matches("handbook", event.key)) {
            menu.game.handbook.open("");
        }
        // Other keys end here as well; Tab moves between the buttons.
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    // Which buttons of the world's group show: save (a saved world), open to LAN (not in a network
    // game), manage server (operators only, decision E-863).
    readonly property bool showSave: menu.game.persistent
    readonly property bool showLan: !menu.game.multiplayer.networkGame
    readonly property bool showAdmin: menu.game.admin.operatorRights
    readonly property list<bool> worldButtons: [menu.showSave, menu.showLan, menu.showAdmin]

    // The columns button `index` of the world's group takes: the last one shown of an odd number
    // fills the row, so no gap stays beside it.
    function worldButtonSpan(index: int): int {
        const shown = menu.worldButtons.filter(flag => flag).length;
        return menu.worldButtons[index] && shown % 2 === 1 && menu.worldButtons.lastIndexOf(true) === index ? 2 : 1;
    }

    // A button of a group: two side by side, as wide as each other, and as tall as the others. Both
    // columns grow from the same basis of 0, so the layout shares the width evenly instead of by
    // the length of the texts.
    component GroupButton: GameButton {
        Layout.fillWidth: true
        Layout.preferredWidth: 0
        large: true
    }

    // Draft D.1 A of docs/Menue-Pruefung.md: a card on ScreenFrame, the buttons in groups apart by
    // Ui.space.xl: continue (primary) · the screens of the game (settings, handbook, character,
    // advancements) · the world (save, open to LAN, manage server) · set apart below a line, the
    // way out. Hidden while one of its screens is open: it would show around a small one. The
    // handbook and the advancements cover it as well (and keep Tab).
    WorldCard {
        id: card

        visible: !settingsScreen.visible && !characterScreen.visible && !lanScreen.visible && !adminScreen.visible
                 && !menu.game.handbook.isOpen
        // Clear of the HUD's status rows behind it (decision E-927) and of the pad's hints.
        reservedBottom: Math.max(menu.statusReserved, menu.game.gamepad.focusShown ? Ui.size.padHints : 0)

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Ui.space.xs

            Label {
                Layout.fillWidth: true
                // The world runs on while others play in it (decision E-877).
                text: menu.game.pause.hint === WorldPause.None ? qsTr("Game paused") : qsTr("Game menu")
                color: Theme.text
                font.pixelSize: Ui.type.title
                font.bold: true
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            Label {
                Layout.fillWidth: true
                visible: menu.game.pause.hint !== WorldPause.None
                text: menu.game.pause.hint === WorldPause.OthersConnected
                      ? qsTr("The game keeps running: other players are connected.")
                      : menu.game.pause.hint === WorldPause.Pending
                        ? qsTr("The game pauses as soon as the world around you has loaded.")
                        : qsTr("The game keeps running on the server.")
                color: Theme.textMuted
                font.pixelSize: Ui.type.help
                wrapMode: Text.WordWrap
            }
        }

        GameButton {
            objectName: "continueGameButton"
            Layout.fillWidth: true
            kind: GameButton.Primary
            large: true
            text: qsTr("Continue")
            keyHint: menu.game.gamepad.active ? menu.game.gamepad.pads.texts.east : qsTr("Esc")
            onClicked: menu.game.overlay = GameController.Overlay.None
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Ui.space.s
            rowSpacing: Ui.space.s

            GroupButton {
                objectName: "pauseSettingsButton"
                text: qsTr("Settings")
                onClicked: menu.game.overlay = GameController.Overlay.Settings
            }

            GroupButton {
                objectName: "pauseHandbookButton"
                text: qsTr("Handbook")
                keyHint: menu.game.settings.keys.labels.handbook
                onClicked: menu.game.handbook.open("")
            }

            GroupButton {
                objectName: "pauseCharacterButton"
                text: qsTr("Character")
                onClicked: menu.game.overlay = GameController.Overlay.Character
            }

            GroupButton {
                objectName: "advancementsButton"
                text: qsTr("Advancements")
                keyHint: menu.game.settings.keys.labels.advancements
                onClicked: menu.game.overlay = GameController.Overlay.Advancements
            }
        }

        GridLayout {
            Layout.fillWidth: true
            visible: menu.worldButtons.some(shown => shown)
            columns: 2
            columnSpacing: Ui.space.s
            rowSpacing: Ui.space.s

            GroupButton {
                objectName: "saveWorldButton"
                visible: menu.showSave
                Layout.columnSpan: menu.worldButtonSpan(0)
                text: qsTr("Save world")
                onClicked: {
                    menu.game.saveWorld();
                    menu.game.overlay = GameController.Overlay.None;
                }
            }

            GroupButton {
                objectName: "openToLanButton"
                visible: menu.showLan
                Layout.columnSpan: menu.worldButtonSpan(1)
                text: menu.game.multiplayer.lanPort > 0 ? qsTr("Open on port %1").arg(menu.game.multiplayer.lanPort) : qsTr("Open to LAN")
                onClicked: menu.game.overlay = GameController.Overlay.Lan
            }

            // Only for operators (decision E-863): the server tells each player whether he is one.
            GroupButton {
                objectName: "manageServerButton"
                visible: menu.showAdmin
                Layout.columnSpan: menu.worldButtonSpan(2)
                text: qsTr("Manage server")
                onClicked: menu.game.overlay = GameController.Overlay.Admin
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Ui.space.xl

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: Ui.size.line
                color: Theme.separator
            }

            LeaveGameButton {
                Layout.fillWidth: true
                game: menu.game
            }
        }
    }

    // The overlays that open the settings (each at its page, SettingsScreen.pageOfOverlay).
    readonly property list<int> settingsPages: [GameController.Overlay.Settings, GameController.Overlay.Display,
        GameController.Overlay.Audio, GameController.Overlay.Interface, GameController.Overlay.Controls,
        GameController.Overlay.Gamepad, GameController.Overlay.Debug, GameController.Overlay.TexturePacks]

    // The settings are an overlay of their own (Overlay.Settings) so the --overlay test hook can
    // open them. Closing a sub-screen returns to the menu and its keys.
    function subScreenClosed(overlays: list<int>) {
        // The debug options open from the world (debug options key) and return there.
        if (menu.game.overlay === GameController.Overlay.Debug)
            menu.game.overlay = GameController.Overlay.None;
        else if (overlays.includes(menu.game.overlay))
            menu.game.overlay = GameController.Overlay.Pause;
        if (visible)
            forceActiveFocus();
    }

    // One level down from the pause menu, as from the main menu (SidebarScreen); the debug options
    // open from the world (debug options key) and lead back there.
    SettingsScreen {
        id: settingsScreen

        game: menu.game
        inGame: true
        // The handbook opened from the settings covers them (and keeps Tab).
        covered: menu.game.handbook.isOpen
        backText: menu.game.overlay === GameController.Overlay.Debug ? qsTr("‹ Back to game") : qsTr("‹ Pause menu")
        onClosed: menu.subScreenClosed(menu.settingsPages)
    }

    CharacterScreen {
        id: characterScreen

        game: menu.game
        onClosed: menu.subScreenClosed([GameController.Overlay.Character])
    }

    LanScreen {
        id: lanScreen

        game: menu.game
        onClosed: menu.subScreenClosed([GameController.Overlay.Lan])
    }

    AdminScreen {
        id: adminScreen

        game: menu.game
        onClosed: menu.subScreenClosed([GameController.Overlay.Admin])
    }

    // One sub-screen at a time, the one the overlay names; any other overlay closes them.
    function followOverlay() {
        const overlay = menu.game.overlay;
        if (!menu.settingsPages.includes(overlay))
            settingsScreen.close();
        if (overlay !== GameController.Overlay.Character)
            characterScreen.close();
        if (overlay !== GameController.Overlay.Lan)
            lanScreen.close();
        if (overlay !== GameController.Overlay.Admin)
            adminScreen.close();
        if (menu.settingsPages.includes(overlay)) {
            settingsScreen.startPage = settingsScreen.pageOfOverlay(overlay);
            settingsScreen.open();
        } else if (overlay === GameController.Overlay.Character) {
            characterScreen.open();
        } else if (overlay === GameController.Overlay.Lan) {
            lanScreen.open();
        } else if (overlay === GameController.Overlay.Admin && !adminScreen.visible) {
            adminScreen.open();
        }
    }

    Connections {
        target: menu.game

        function onOverlayChanged() {
            menu.followOverlay();
        }
    }
}
