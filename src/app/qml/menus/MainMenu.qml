pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The main menu (variant C of docs/UI-Leitfaden.md, decision E-852): a side bar with the logo, the
// entries, the player's profile and Quit; on the right the page of the chosen entry: the start page
// (continue the world played last, the list of worlds) with the page for a new world, multiplayer,
// character, and mods & packs. Esc and the pad's B go back to the start page. The settings and the
// handbook take the side bar one level down (SidebarScreen, drill-down): the bar shows a back entry
// and their categories, the content their page; back, the entry has the focus again.
Rectangle {
    id: menu

    enum Page {
        Home,
        NewWorld,
        Multiplayer,
        Character,
        Mods
    }

    required property GameController game
    property int page: MainMenu.Home
    // A screen or dialog of the menu is open.
    readonly property bool covered: settingsScreen.visible || deleteDialog.visible || game.handbook.isOpen
    // Gives the focus back when the settings close: to where they were opened from (the mods page's
    // "Texture packs …"), else to the side bar's "Settings".
    property var settingsReturn: null

    signal quitRequested

    objectName: "mainMenu"
    gradient: Gradient {
        GradientStop { position: 0.0; color: Theme.menuTop }
        GradientStop { position: 1.0; color: Theme.menuBottom }
    }

    focus: visible
    onVisibleChanged: {
        if (visible) {
            game.library.refreshWorlds();
            page = MainMenu.Home;
            forceActiveFocus();
        } else {
            // Its sub-screens belong to the menu and close with it; an open draft of the character
            // closes unapplied.
            settingsScreen.close();
            deleteDialog.close();
            characterPage.close();
        }
    }
    // Keys the text fields and buttons leave; Tab moves the focus, Esc goes back to the start page.
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape && menu.leavesPage())
            menu.navigate(() => menu.showHome());
        else if (menu.game.settings.keys.matches("handbook", event.key) && !menu.game.handbook.isOpen
                 && !menu.game.multiplayer.joining)
            menu.game.handbook.open("");
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    // Opens the multiplayer page; with an address it joins from there (--overlay multiplayer).
    function openMultiplayer(address: string, name: string, password: string) {
        navigate(() => {
            multiplayerPage.open();
            if (address.length > 0)
                multiplayerPage.joinServer(address, name, password);
        });
    }

    // Runs `action`, which leaves the shown page; the character page first asks about unsaved
    // changes (save, discard or keep editing, which runs nothing).
    function navigate(action: var) {
        if (page === MainMenu.Character)
            characterPage.leave(action);
        else
            action();
    }

    // Esc or B leaves the shown page for the start page; not on the start page and not while a
    // join runs (its Cancel stops it).
    function leavesPage(): bool {
        return page !== MainMenu.Home && !(page === MainMenu.Multiplayer && game.multiplayer.joining);
    }

    // The side bar's entry that belongs to a page.
    function entryOf(shown: int): int {
        switch (shown) {
        case MainMenu.Multiplayer:
            return MenuSidebar.Multiplayer;
        case MainMenu.Character:
            return MenuSidebar.Character;
        case MainMenu.Mods:
            return MenuSidebar.Mods;
        default:
            return MenuSidebar.Play;
        }
    }

    function subScreenClosed() {
        if (visible)
            forceActiveFocus();
    }

    // Back from the settings: the item they were opened from has the focus again.
    function settingsClosed() {
        const back = settingsReturn;
        settingsReturn = null;
        if (!visible)
            return;

        if (back)
            back();
        else
            sidebar.focusEntry(MenuSidebar.Settings);
    }

    function showHome() {
        page = MainMenu.Home;
        forceActiveFocus();
    }

    // The pad's B (Escape) leaves the shown page for the start page (QcMenuNavigator asks the
    // items above the focus before it sends Escape).
    function gamepadCommand(command: string): bool {
        if (command !== "back" || !leavesPage())
            return false;

        navigate(() => showHome());
        return true;
    }

    function openSettings(startPage: int, back: var) {
        settingsReturn = back;
        settingsScreen.startPage = startPage;
        settingsScreen.open();
    }

    // A side bar entry: the handbook and the settings open over the page, the others leave it.
    // Nothing while a join runs: the multiplayer page shows it, and only its Cancel stops it (a
    // world started meanwhile would be replaced when the join succeeds).
    function choose(entry: int) {
        if (game.multiplayer.joining && entry !== MenuSidebar.Quit)
            return;
        if (entry === entryOf(page) && entry !== MenuSidebar.Play)
            return;

        if (entry === MenuSidebar.Handbook || entry === MenuSidebar.Settings || entry === entryOf(page))
            show(entry);
        else
            navigate(() => show(entry));
    }

    function show(entry: int) {
        switch (entry) {
        case MenuSidebar.Play:
            showHome();
            break;
        case MenuSidebar.Multiplayer:
            multiplayerPage.open();
            break;
        case MenuSidebar.Character:
            characterPage.open();
            break;
        case MenuSidebar.Handbook:
            game.handbook.open("");
            break;
        case MenuSidebar.Mods:
            modsPage.open();
            break;
        case MenuSidebar.Settings:
            openSettings(SettingsScreen.Graphics, null);
            break;
        case MenuSidebar.Quit:
            quitRequested();
            break;
        }
    }

    MenuSidebar {
        id: sidebar

        objectName: "menuSidebar"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Ui.size.menuNav
        // The settings show their own bar in its place; the handbook covers it (and keeps Tab).
        visible: !settingsScreen.visible && !menu.game.handbook.isOpen
        game: menu.game
        // During a join only its Cancel (and Quit) can be used.
        locked: menu.game.multiplayer.joining
        current: menu.entryOf(menu.page)
        // The character page shows the figure itself.
        covered: menu.covered || menu.page === MainMenu.Character
        onChosen: entry => menu.choose(entry)
    }

    // The page keeps the menu's margin to the window and is no wider than a large screen; in a
    // window wider than that (a large monitor in fullscreen) it stands in the middle of the room
    // beside the side bar rather than at its left edge.
    StackLayout {
        objectName: "menuPages"
        visible: !settingsScreen.visible && !menu.game.handbook.isOpen
        x: Ui.pageX(sidebar.width, parent.width, width)
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: Ui.space.xl
        // Room for the pad's hints (PadHints) while they show.
        anchors.bottomMargin: Ui.space.xl + (menu.game.gamepad.focusShown ? Ui.size.padHints : 0)
        width: Ui.pageWidth(sidebar.width, parent.width)
        currentIndex: menu.page

        HomePage {
            game: menu.game
            onPlayRequested: folder => menu.game.playWorld(folder)
            onNewWorldRequested: newWorldPage.open()
            onDeleteRequested: (folder, name) => {
                deleteDialog.folder = folder;
                deleteDialog.worldName = name;
                deleteDialog.open();
            }
        }

        NewWorldPage {
            id: newWorldPage

            objectName: "newWorldPage"
            game: menu.game
            onOpenRequested: menu.page = MainMenu.NewWorld
            onBackRequested: menu.showHome()
        }

        MultiplayerPage {
            id: multiplayerPage

            objectName: "multiplayerPage"
            game: menu.game
            onOpenRequested: menu.page = MainMenu.Multiplayer
            onJoined: menu.showHome()
        }

        CharacterPage {
            id: characterPage

            objectName: "menuCharacterScreen"
            game: menu.game
            onOpenRequested: menu.page = MainMenu.Character
        }

        ModsPage {
            id: modsPage

            objectName: "modsScreen"
            game: menu.game
            onOpenRequested: menu.page = MainMenu.Mods
            onTexturePacksRequested: menu.openSettings(SettingsScreen.TexturePacks, () => modsPage.focusTexturePacks())
        }
    }

    SettingsScreen {
        id: settingsScreen

        objectName: "menuSettingsScreen"
        game: menu.game
        backText: qsTr("‹ Main menu")
        // The handbook opened from the settings covers them (and keeps Tab).
        covered: menu.game.handbook.isOpen
        onClosed: menu.settingsClosed()
    }

    // A join started elsewhere ("join again", --join) shows its progress here; the character page
    // asks about unsaved changes first.
    Connections {
        target: menu.game.multiplayer

        function onJoiningChanged() {
            if (menu.visible && menu.game.multiplayer.joining && menu.page !== MainMenu.Multiplayer)
                menu.navigate(() => multiplayerPage.open());
        }
    }

    DeleteWorldDialog {
        id: deleteDialog

        objectName: "deleteWorldDialog"
        onAccepted: menu.game.library.deleteWorld(deleteDialog.folder)
        onClosed: menu.subScreenClosed()
    }
}
