pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The game settings (decision E-845, docs/UI-Leitfaden.md 8 and 8b): one level down the side bar
// (SidebarScreen), with a back entry and the categories (graphics with the display options of
// decision E-835, audio, controls, controller, interface, texture packs, advanced with debug and
// diagnostics) in the bar and the chosen page on the right, scrolling as a whole. Changes apply at
// once and are stored when the screen closes. The back entry, Esc and the pad's B close it; the
// pad's shoulders and Ctrl+Tab switch the category. The main menu and the pause menu show the same
// screen.
SidebarScreen {
    id: screen

    enum Page {
        Graphics,
        Audio,
        Controls,
        Controller,
        Interface,
        TexturePacks,
        Advanced
    }

    required property GameController game
    // The page to open on (SettingsScreen.Graphics, ...; pageOfOverlay() for an overlay).
    property int startPage: SettingsScreen.Graphics

    // The screen was closed (back entry, Esc, the pad's B or close()).
    signal closed

    // The page an overlay opens: the settings and the display options the graphics page (it begins
    // with them), the debug options the advanced page.
    function pageOfOverlay(overlay: int): int {
        switch (overlay) {
        case GameController.Overlay.Audio:
            return SettingsScreen.Audio;
        case GameController.Overlay.Controls:
            return SettingsScreen.Controls;
        case GameController.Overlay.Gamepad:
            return SettingsScreen.Controller;
        case GameController.Overlay.Interface:
            return SettingsScreen.Interface;
        case GameController.Overlay.TexturePacks:
            return SettingsScreen.TexturePacks;
        case GameController.Overlay.Debug:
            return SettingsScreen.Advanced;
        default:
            return SettingsScreen.Graphics;
        }
    }

    // Opens on `startPage` with the focus on its first setting (docs/UI-Leitfaden.md 10).
    function open() {
        nav.currentIndex = startPage;
        controlsPage.clear();
        visible = true;
        forceActiveFocus();
        focusFirstSetting();
    }

    // The first item of the shown page that takes the focus, as Tab would reach it.
    function focusFirstSetting() {
        const page = pages.children[pages.currentIndex];
        const first = page ? page.nextItemInFocusChain(true) : null;
        for (let item = first; item; item = item.parent) {
            if (item === page) {
                first.forceActiveFocus(Qt.TabFocusReason);
                return;
            }
        }
    }

    function close() {
        if (!visible)
            return;

        controlsPage.cancelCapture();
        visible = false;
        game.settings.saveSettings();
        closed();
    }

    // The pad's shoulders switch the category, its B closes the screen (QcMenuNavigator asks the
    // items above the focus).
    function gamepadCommand(command: string): bool {
        if (command === "back") {
            close();
            return true;
        }
        return nav.takeCommand(command);
    }

    anchors.fill: parent
    visible: false
    title: qsTr("Settings")
    bottomRoom: game.gamepad.focusShown ? Ui.size.padHints : 0
    onBackRequested: close()
    // Keys the controls leave end here; Esc closes the screen, Tab moves the focus (Ctrl+Tab and
    // Ctrl+Shift+Tab switch the category: shortcuts of the NavList).
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape)
            screen.close();
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    entries: [
        NavList {
            id: nav

            objectName: "settingsNav"
            Layout.fillWidth: true
            model: [qsTr("Graphics"), qsTr("Audio"), qsTr("Controls"), qsTr("Controller"), qsTr("Interface"),
                qsTr("Texture packs"), qsTr("Advanced")]
        },
        // Keeps the categories at the top.
        Item {
            Layout.fillHeight: true
        }
    ]

    StackLayout {
        id: pages

        anchors.fill: parent
        currentIndex: nav.currentIndex
        onCurrentIndexChanged: controlsPage.cancelCapture()

        GraphicsPage {
            game: screen.game
        }
        AudioPage {
            game: screen.game
        }
        ControlsPage {
            id: controlsPage

            game: screen.game
        }
        GamepadPage {
            game: screen.game
        }
        InterfacePage {
            game: screen.game
        }
        TexturePacksPage {
            game: screen.game
        }
        AdvancedPage {
            game: screen.game
        }
    }
}
