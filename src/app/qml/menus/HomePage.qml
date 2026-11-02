// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The main menu's start page (variant C, decision E-852): a note after a crash, the card to continue
// the world played last (or to create the first one, with ways into the handbook, multiplayer and
// the character below it: HM-4 of docs/Menue-Pruefung.md), and the other saved worlds with Play and
// a "…" menu, the last played first; the card's world is not repeated in the list (HM-1). The list
// scrolls on its own.
ColumnLayout {
    id: page

    required property GameController game
    readonly property var worlds: game.library.worlds
    // The world played last that can be opened, else null (WorldLibrary.continueWorld).
    readonly property var lastWorld: game.library.continueWorld
    // The worlds of the list: all but the card's.
    readonly property var otherWorlds: game.library.otherWorlds

    // The room on the right of the list for its scroll bar; the card and the heading above keep
    // it too, so their right edges line up with the rows.
    readonly property real gutter: Ui.space.l

    signal playRequested(string folder)
    signal deleteRequested(string folder, string name)
    signal newWorldRequested
    // A page of the menu beside the bar should show (MenuSidebar.Multiplayer, Character).
    signal entryRequested(int entry)

    // The game mode of new players (GameMode.Mode).
    function modeText(mode: int): string {
        switch (mode) {
        case GameMode.Mode.Creative:
            return qsTr("Creative");
        case GameMode.Mode.Adventure:
            return qsTr("Adventure");
        case GameMode.Mode.Spectator:
            return qsTr("Spectator");
        default:
            return qsTr("Survival");
        }
    }

    // The world's type; with `withSeed`, a normal world's seed too.
    function typeText(world: var, withSeed: bool): string {
        switch (world.worldType) {
        case WorldLibrary.WorldType.Flat:
            return qsTr("Flat");
        case WorldLibrary.WorldType.Void:
            return qsTr("Void");
        case WorldLibrary.WorldType.Showcase:
            return qsTr("Showcase");
        default:
            return withSeed ? qsTr("Normal, seed %1").arg(world.seed) : qsTr("Normal");
        }
    }

    // When the world was played, its type and its mode, as one line.
    function summary(world: var, withSeed: bool): string {
        return [world.lastPlayed, typeText(world, withSeed), modeText(world.gameMode)].join(" · ");
    }

    spacing: Ui.space.m

    // The previous session ended with a crash (decision E-834): where its report is.
    RowLayout {
        objectName: "crashNotice"
        Layout.fillWidth: true
        Layout.rightMargin: page.gutter
        visible: page.game.settings.diagnostics.crashReport !== ""
        spacing: Ui.space.s

        Hint {
            Layout.fillWidth: true
            kind: Hint.Warning
            text: qsTr("The last session ended with a crash. Its report is %1; please send it with the log.")
                      .arg(page.game.settings.diagnostics.crashReport)
        }
        GameButton {
            text: qsTr("Open log folder")
            onClicked: page.game.settings.diagnostics.openLogFolder()
        }
        GameButton {
            kind: GameButton.Quiet
            text: qsTr("Dismiss")
            onClicked: page.game.settings.diagnostics.dismissCrashReport()
        }
    }

    ContinueCard {
        Layout.fillWidth: true
        Layout.rightMargin: page.gutter
        world: page.lastWorld
        summary: page.lastWorld !== null ? page.summary(page.lastWorld, false) : ""
        hasWorlds: page.worlds.length > 0
        onContinueRequested: folder => page.playRequested(folder)
        onNewWorldRequested: page.newWorldRequested()
    }

    // The first start: three ways on besides the first world.
    RowLayout {
        objectName: "firstStartLinks"
        Layout.rightMargin: page.gutter
        visible: page.worlds.length === 0
        spacing: Ui.space.s

        GameButton {
            text: qsTr("Handbook: first steps")
            keyHint: page.game.settings.keys.labels.handbook
            onClicked: page.game.handbook.open("chapter:qtcraft:basics")
        }
        GameButton {
            text: qsTr("Multiplayer")
            onClicked: page.entryRequested(MenuSidebar.Multiplayer)
        }
        GameButton {
            text: qsTr("Character")
            onClicked: page.entryRequested(MenuSidebar.Character)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Ui.space.s
        Layout.rightMargin: page.gutter
        visible: page.lastWorld !== null || page.otherWorlds.length > 0
        spacing: Ui.space.s

        Label {
            id: listHeading

            Layout.fillWidth: true
            // No heading above an empty list (only the card's world is saved).
            visible: page.otherWorlds.length > 0
            text: page.lastWorld !== null ? qsTr("More worlds") : qsTr("Your worlds")
            font.pixelSize: Ui.type.section
            font.bold: true
            color: Theme.text
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
        // Keeps "New world…" on the right without the heading.
        Item {
            Layout.fillWidth: true
            visible: !listHeading.visible
        }
        GameButton {
            objectName: "newWorldButton"
            // The empty page offers it in the card instead.
            visible: page.lastWorld !== null
            text: qsTr("New world…")
            onClicked: page.newWorldRequested()
        }
    }

    ListView {
        id: worldList

        objectName: "worldList"
        Accessible.name: qsTr("Your worlds")
        Accessible.role: Accessible.List
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: count > 0
        clip: true
        spacing: Ui.space.s
        boundsBehavior: Flickable.StopAtBounds
        model: page.otherWorlds
        ScrollBar.vertical: ThemedScrollBar {}

        delegate: WorldRow {
            id: worldRow

            width: ListView.view.width - page.gutter
            summary: page.summary(worldRow.modelData, true)
            onPlayRequested: folder => page.playRequested(folder)
            onDeleteRequested: (folder, name) => page.deleteRequested(folder, name)
            onOpenFolderRequested: folder => page.game.library.openWorldFolder(folder)
        }
    }

    // Keeps the card at the top while there is no list.
    Item {
        Layout.fillHeight: true
        visible: !worldList.visible
    }
}
