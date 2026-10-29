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
        focusPage();
    }

    function close() {
        if (!visible)
            return;

        controlsPage.cancelCapture();
        visible = false;
        game.settings.saveSettings();
        closed();
    }

    // The pad's shoulders switch the category, its B closes the screen, its Y resets a page that
    // has defaults to go back to after a second Y (QcMenuNavigator asks the items above the focus).
    // Any other command cancels a reset that waits for its second Y.
    function gamepadCommand(command: string): bool {
        if (command !== "tertiary")
            cancelReset();
        if (command === "back") {
            close();
            return true;
        }
        if (command === "tertiary" && resettable) {
            padReset();
            return true;
        }
        return nav.takeCommand(command);
    }

    // The pad's Y: the first asks, the second resets the page; the focus goes to the page's reset
    // button (scrolled into view) and the notice says what the reset did (decision E-925).
    function padReset() {
        const page = shownPage as SettingsPage;
        if (!confirmReset()) {
            showNotice(qsTr("Press %1 again to reset this page to its defaults.").arg(game.gamepad.pads.texts.north));
            return;
        }

        page.resetRequested(true);
        if (page.resetButton) {
            page.resetButton.forceActiveFocus(Qt.TabFocusReason);
            // Once a rebuilt key table is laid out.
            Qt.callLater(() => page.reveal(page.resetButton));
        }
        showNotice(page.message);
    }

    anchors.fill: parent
    visible: false
    title: qsTr("Settings")
    bottomRoom: game.gamepad.focusShown ? Ui.size.padHints : 0
    shownPage: pages.children[pages.currentIndex] ?? null
    resettable: (shownPage as SettingsPage)?.resettable ?? false
    onBackRequested: close()
    // The mouse or the keyboard took over: a reset waiting for its second Y is off.
    Connections {
        target: screen.game.gamepad

        function onActiveChanged() {
            if (!screen.game.gamepad.active)
                screen.cancelReset();
        }
    }
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
            onSwitched: screen.focusPage()
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
