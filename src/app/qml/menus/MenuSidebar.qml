pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The main menu's side bar (variant C of docs/UI-Leitfaden.md, decision E-852): the logo, the
// menu's entries (play, multiplayer, character and mods & packs switch the page beside the bar, the
// chosen one marked; the handbook and the settings take the bar one level down, SidebarScreen),
// the player's profile and Quit at the foot. Up and down move along the entries as Tab does; Enter, Space and the pad's
// A choose one.
SideBar {
    id: bar

    enum Entry {
        Play,
        Multiplayer,
        Character,
        Handbook,
        Mods,
        Settings,
        Quit
    }

    required property GameController game
    // Another screen covers the menu: the profile's figure stops moving.
    property bool covered: false
    // The entry whose page the menu shows (MenuSidebar.Play, Multiplayer, Character or Mods).
    property int current: MenuSidebar.Play
    // A join runs: the entries and the profile are disabled until it ends (its Cancel stays on the
    // page); Quit stays.
    property bool locked: false

    signal chosen(int entry)

    // Up and down move the focus along the bar's entries (and on into the page, as Tab does).
    function moveFocus(item: Item, forward: bool) {
        item.nextItemInFocusChain(forward)?.forceActiveFocus(Qt.TabFocusReason);
    }

    // Gives the focus to an entry (MenuSidebar.Settings, ...), as Tab would: back from a screen one
    // level down, its entry has the focus again.
    function focusEntry(entry: int) {
        for (const item of entryColumn.children) {
            if (item.objectName === "menuEntry" + entry)
                item.forceActiveFocus(Qt.TabFocusReason);
        }
    }

    ColumnLayout {
        id: entryColumn

        Layout.fillWidth: true
        spacing: 0

        Repeater {
            model: [
                { entry: MenuSidebar.Play, text: qsTr("Play"), key: "" },
                { entry: MenuSidebar.Multiplayer, text: qsTr("Multiplayer"), key: "" },
                { entry: MenuSidebar.Character, text: qsTr("Character"), key: "" },
                { entry: MenuSidebar.Handbook, text: qsTr("Handbook"), key: bar.game.settings.keys.labels.handbook },
                { entry: MenuSidebar.Mods, text: qsTr("Mods & packs"), key: "" },
                { entry: MenuSidebar.Settings, text: qsTr("Settings"), key: "" }
            ]

            delegate: NavButton {
                id: entryButton

                required property var modelData

                objectName: "menuEntry" + modelData.entry
                // Settings and the handbook take the bar one level down; the others are its pages.
                page: modelData.entry !== MenuSidebar.Handbook && modelData.entry !== MenuSidebar.Settings
                checked: modelData.entry === bar.current
                enabled: !bar.locked
                text: modelData.text
                keyHint: modelData.key
                onClicked: bar.chosen(modelData.entry)
                Keys.onUpPressed: bar.moveFocus(entryButton, false)
                Keys.onDownPressed: bar.moveFocus(entryButton, true)
            }
        }
    }

    // Keeps the profile and Quit at the foot.
    Item {
        Layout.fillHeight: true
    }

    ProfileCard {
        id: profile

        game: bar.game
        covered: bar.covered
        enabled: !bar.locked
        onClicked: bar.chosen(MenuSidebar.Character)
        Keys.onUpPressed: bar.moveFocus(profile, false)
        Keys.onDownPressed: bar.moveFocus(profile, true)
    }

    NavButton {
        id: quitEntry

        objectName: "quitButton"
        text: qsTr("Quit")
        onClicked: bar.chosen(MenuSidebar.Quit)
        Keys.onUpPressed: bar.moveFocus(quitEntry, false)
        Keys.onDownPressed: bar.moveFocus(quitEntry, true)
    }
}
