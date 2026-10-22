pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The main menu's start page (variant C, decision E-852): a note after a crash, the card to continue
// the world played last (or to create the first one), and every saved world with Play and Delete,
// the last played first. The list scrolls on its own.
ColumnLayout {
    id: page

    required property GameController game
    readonly property var worlds: game.library.worlds
    // The world played last that can be opened, else null.
    readonly property var lastWorld: worlds.find(world => world.error.length === 0) ?? null

    // The room on the right of the list for its scroll bar; the card and the heading above keep
    // it too, so their right edges line up with the rows.
    readonly property real gutter: Ui.space.l

    signal playRequested(string folder)
    signal deleteRequested(string folder, string name)
    signal newWorldRequested

    function modeText(mode: string): string {
        switch (mode) {
        case "creative":
            return qsTr("Creative");
        case "adventure":
            return qsTr("Adventure");
        case "spectator":
            return qsTr("Spectator");
        default:
            return qsTr("Survival");
        }
    }

    // The world's type; with `withSeed`, a normal world's seed too.
    function typeText(world: var, withSeed: bool): string {
        switch (world.generator) {
        case "flat":
            return qsTr("Flat");
        case "void":
            return qsTr("Void");
        case "showcase":
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

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Ui.space.s
        Layout.rightMargin: page.gutter
        visible: page.worlds.length > 0
        spacing: Ui.space.s

        Label {
            Layout.fillWidth: true
            text: qsTr("Your worlds")
            font.pixelSize: Ui.type.section
            font.bold: true
            color: Theme.text
            Accessible.role: Accessible.Heading
            Accessible.name: text
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
        model: page.worlds
        ScrollBar.vertical: ThemedScrollBar {}

        delegate: WorldRow {
            id: worldRow

            width: ListView.view.width - page.gutter
            summary: page.summary(worldRow.modelData, true)
            onPlayRequested: folder => page.playRequested(folder)
            onDeleteRequested: (folder, name) => page.deleteRequested(folder, name)
        }
    }

    // Keeps the card at the top while there is no list.
    Item {
        Layout.fillHeight: true
        visible: !worldList.visible
    }
}
