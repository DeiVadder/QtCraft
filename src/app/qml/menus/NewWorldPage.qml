// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtCraft

// The main menu's page for a new world (variant C, decision E-852; draft D.3 A of
// docs/Menue-Pruefung.md, decision E-930): on the left the world (name, type with its seed and the
// die, or the layers), the rules (game mode, keeping the inventory) and for a normal world its world
// options (their preset or changes, and "Customise …" to their page, decision E-917); on the right a
// small map preview (decision E-921). The groups scroll as a whole; "‹ Back" and "Create and play"
// stay in the footer. Esc and the pad's B go back (MainMenu).
ColumnLayout {
    id: page

    required property GameController game
    // The form's values and defaults (decision E-956); the map preview follows its seed, also the
    // random one of an empty seed field (decision E-921).
    readonly property NewWorldDraft draft: game.worldOptions.draft
    // A normal world: it has world options and a map preview.
    readonly property bool terrain: draft.terrain

    // The page should be shown (open(), also the test hook "@<ms> open:newWorldPage").
    signal openRequested
    signal backRequested
    // "Customise …": the page of the world generation options (WorldOptionsPage).
    signal optionsRequested

    // Shows the form with its defaults, the name chosen for typing over it.
    function open() {
        page.draft.reset();
        form.contentY = 0;
        openRequested();
        worldName.forceActiveFocus(Qt.TabFocusReason);
        worldName.selectAll();
    }

    function create() {
        page.game.createWorld(page.draft.name, page.draft.seed, page.draft.setup());
    }

    // Back from the options: the small map shows the default view again (the options' map may
    // have moved or zoomed), and the button that opened them has the focus again.
    function backFromOptions() {
        page.game.worldOptions.preview.resetView();
        moreOptions.forceActiveFocus(Qt.TabFocusReason);
    }

    // Whether the keyboard's focus is in `item` (the control of a setting row is a child of it).
    function holdsFocus(item: Item): bool {
        for (let at = Window.activeFocusItem; at; at = at.parent) {
            if (at === item)
                return true;
        }
        return false;
    }

    // The pad's down from the form's last setting goes to "Create and play": the footer lies too
    // far to the side of it for the spatial step (the map column has no focus target).
    function gamepadCommand(command: int): bool {
        if (command !== GamepadMenuInput.Command.Down || !page.holdsFocus(page.terrain ? moreOptions : keepInventory))
            return false;
        createButton.forceActiveFocus(Qt.TabFocusReason);
        return true;
    }

    // A die showing five, the symbol of the random seed's button.
    component DieIcon: Rectangle {
        id: die

        readonly property real pip: Math.max(2 * Ui.size.line, Math.round(width / 5))

        implicitWidth: Ui.size.icon
        implicitHeight: Ui.size.icon
        radius: Ui.radius
        color: Theme.text
        border.color: Theme.knobOutline
        border.width: Ui.size.line

        Repeater {
            model: [Qt.point(0.27, 0.27), Qt.point(0.73, 0.27), Qt.point(0.5, 0.5), Qt.point(0.27, 0.73), Qt.point(0.73, 0.73)]

            delegate: Rectangle {
                required property point modelData

                x: modelData.x * die.width - width / 2
                y: modelData.y * die.height - height / 2
                width: die.pip
                height: die.pip
                color: Theme.knobOutline
            }
        }
    }

    spacing: 0

    // The dropdowns show the draft's choices, also after it was reset (and once their lists are
    // filled). The text fields bind the draft's texts and write every change back (also one a test
    // hook sets), the dropdowns and the switch the player's choice.
    Binding {
        target: worldType
        property: "currentIndex"
        value: worldType.count > 0 ? worldType.indexOfValue(page.draft.worldType) : -1
    }
    Binding {
        target: gameMode
        property: "currentIndex"
        value: gameMode.count > 0 ? gameMode.indexOfValue(page.draft.gameMode) : -1
    }

    SettingsPage {
        id: form

        objectName: "newWorldForm"
        Layout.fillWidth: true
        Layout.fillHeight: true
        title: qsTr("New world")

        // Draft D.3 A of docs/Menue-Pruefung.md: world, rules and world options on the left, the
        // small map on the right; one column where the page is too narrow for both, the map then at
        // the end, below the world options.
        GridLayout {
            id: layoutColumns

            readonly property real mapColumn: Ui.size.map + 2 * Ui.space.l
            readonly property bool beside: page.terrain && width >= Ui.screen.s + mapColumn + columnSpacing

            Layout.fillWidth: true
            columns: beside ? 2 : 1
            columnSpacing: Ui.space.xl
            rowSpacing: Ui.space.xl

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Ui.space.xl

                SettingGroup {
                    title: qsTr("World")

                    SettingRow {
                        label: qsTr("Name")

                        GameTextField {
                            id: worldName

                            objectName: "worldName"
                            Layout.fillWidth: true
                            Accessible.name: qsTr("World name")
                            text: page.draft.name
                            onTextChanged: page.draft.name = text
                            onAccepted: page.create()
                        }
                    }
                    SettingDropdown {
                        id: worldType

                        objectName: "worldTypeChoice"
                        label: qsTr("World type")
                        // Each type with what it is.
                        readonly property var choices: [
                            { value: WorldLibrary.WorldType.Terrain, text: qsTr("Normal"), help: qsTr("Hills, mountains, caves, oceans and biomes.") },
                            { value: WorldLibrary.WorldType.Flat, text: qsTr("Flat"), help: qsTr("The same layers everywhere, no caves or trees, no creatures of their own accord.") },
                            { value: WorldLibrary.WorldType.Void, text: qsTr("Void"), help: qsTr("Nothing but a small platform; noon, no creatures of their own accord.") },
                            { value: WorldLibrary.WorldType.Showcase, text: qsTr("Showcase"), help: qsTr("A flat world with every block, creature and item on show, at noon.") }
                        ]

                        help: worldType.choices[worldType.currentIndex]?.help ?? ""
                        textRole: "text"
                        valueRole: "value"
                        model: worldType.choices
                        onActivated: index => page.draft.worldType = worldType.choices[index].value
                    }
                    // The field shows the random seed as its placeholder while it is empty, so the
                    // map always shows the seed the field stands for; the die draws a new one.
                    SettingRow {
                        visible: page.terrain
                        label: qsTr("Seed")
                        help: qsTr("A number or a text; empty for a random one.")

                        GameTextField {
                            id: seed

                            objectName: "worldSeed"
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Seed")
                            text: page.draft.seedText
                            placeholderText: page.draft.randomSeed
                            onTextChanged: page.draft.seedText = text
                            onAccepted: page.create()
                        }
                        GameButton {
                            id: dice

                            objectName: "worldSeedRandom"
                            // As high as the field: the pad's down from the type reaches the field.
                            implicitHeight: seed.implicitHeight
                            leftPadding: Ui.space.s + Ui.size.icon + Ui.space.s
                            text: qsTr("Roll")
                            Accessible.name: qsTr("Random seed")
                            onClicked: page.draft.rollSeed()

                            DieIcon {
                                x: Ui.space.s
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            ThemedToolTip {
                                text: qsTr("Draws a new random seed.")
                                shown: dice.hovered
                            }
                        }
                    }
                    SettingRow {
                        visible: page.draft.worldType === WorldLibrary.WorldType.Flat
                        label: qsTr("Layers")
                        help: qsTr("From the bottom up; empty for the default.")

                        GameTextField {
                            id: flatLayers

                            Layout.fillWidth: true
                            Accessible.name: qsTr("Layers from the bottom up")
                            text: page.draft.layers
                            placeholderText: visible ? page.game.defaultFlatLayers() : ""
                            onTextChanged: page.draft.layers = text
                            onAccepted: page.create()
                        }
                    }
                }

                SettingGroup {
                    title: qsTr("Rules")

                    SettingDropdown {
                        id: gameMode

                        objectName: "gameModeChoice"
                        label: qsTr("Game mode")
                        // Each mode with what it is.
                        readonly property var choices: [
                            { value: GameMode.Mode.Survival, text: qsTr("Survival"), help: qsTr("Gather, craft and survive: health, hunger and creatures count.") },
                            { value: GameMode.Mode.Creative, text: qsTr("Creative"), help: qsTr("Every block and item without limit, flying, instant breaking, no damage or hunger.") },
                            { value: GameMode.Mode.Adventure, text: qsTr("Adventure"), help: qsTr("Survival for prepared maps: blocks break and items are placed only where /adventure allows it.") },
                            { value: GameMode.Mode.Spectator, text: qsTr("Spectator"), help: qsTr("Fly through blocks and look around, unseen; nothing can be touched.") }
                        ]

                        help: gameMode.choices[gameMode.currentIndex]?.help ?? ""
                        textRole: "text"
                        valueRole: "value"
                        model: gameMode.choices
                        onActivated: index => page.draft.gameMode = gameMode.choices[index].value
                    }
                    SettingSwitch {
                        id: keepInventory

                        objectName: "keepInventory"
                        label: qsTr("Keep inventory on death")
                        checked: page.draft.keepInventory
                        onToggled: page.draft.keepInventory = checked
                    }
                }

                SettingGroup {
                    visible: page.terrain
                    title: qsTr("World generation")

                    SettingRow {
                        label: qsTr("World options")
                        help: page.game.worldOptions.summary

                        // At the start of the control column like the switch above it, so the
                        // pad's down reaches it from there.
                        GameButton {
                            id: moreOptions

                            objectName: "moreWorldOptions"
                            text: qsTr("Customise …")
                            Accessible.name: qsTr("Customise the world options")
                            onClicked: page.optionsRequested()
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            // The small map: biomes, sea and rivers with the most common biomes; zoom, the full
            // legend and the villages are on the world options page.
            MapPreviewGroup {
                objectName: "newWorldMap"
                Layout.preferredWidth: layoutColumns.beside ? layoutColumns.mapColumn : -1
                Layout.fillWidth: !layoutColumns.beside
                Layout.alignment: Qt.AlignTop
                visible: page.terrain
                compact: true
                preview: page.game.worldOptions.preview
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: Ui.size.line
        color: Theme.separator
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Ui.space.l
        spacing: Ui.space.s

        GameButton {
            kind: GameButton.Quiet
            text: qsTr("‹ Back")
            Accessible.name: qsTr("Back to the start page")
            onClicked: page.backRequested()
        }
        Item {
            Layout.fillWidth: true
        }
        GameButton {
            id: createButton

            objectName: "createWorldButton"
            kind: GameButton.Primary
            large: true
            text: qsTr("Create and play")
            onClicked: page.create()
        }
    }
}
