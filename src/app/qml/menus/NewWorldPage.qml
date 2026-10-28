pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The main menu's page for a new world (variant C, decision E-852): name, type with its seed or
// layers, the world generation of a normal world (its preset or changes, and the way to "More world
// options", decision E-917) and its map preview (decision E-921), game mode and the rule on keeping the inventory, in groups that scroll
// as a whole; "‹ Back" and "Create and play" stay in the footer. Esc and the pad's B go back
// (MainMenu).
ColumnLayout {
    id: page

    required property GameController game

    // The page should be shown (open(), also the test hook "@<ms> open:newWorldPage").
    // The type and mode the form starts with (indexes into their lists; the test hooks
    // QTCRAFT_MENU_WORLD_TYPE and QTCRAFT_MENU_GAME_MODE set them).
    property int startWorldType: 0
    property int startGameMode: 0
    // The seed of a world whose seed field stays empty, drawn when the form opens, so that the map
    // preview shows the world that will be created (decision E-921).
    property string randomSeed: ""
    // A normal world: it has world options and a map preview.
    readonly property bool terrain: worldType.currentValue === "terrain"

    signal openRequested
    signal backRequested
    // "More world options …": the page of the world generation options (WorldOptionsPage).
    signal optionsRequested

    // Shows the form with its defaults, the name chosen for typing over it.
    function open() {
        worldName.text = qsTr("New World");
        worldType.currentIndex = startWorldType;
        seed.text = "";
        drawSeed();
        flatLayers.text = "";
        gameMode.currentIndex = startGameMode;
        keepInventory.checked = true;
        page.game.worldOptions.reset();
        form.contentY = 0;
        openRequested();
        worldName.forceActiveFocus(Qt.TabFocusReason);
        worldName.selectAll();
    }

    // A new random seed for an empty seed field.
    function drawSeed() {
        randomSeed = String(Math.floor(Math.random() * 2147483647));
    }

    // The seed the world gets: the field's text, else the random one.
    function worldSeed(): string {
        const seedText = seed.text.trim();
        return seedText.length > 0 ? seedText : randomSeed;
    }

    function create() {
        page.game.createWorld(worldName.text, page.worldSeed(), {
            keepInventory: keepInventory.checked,
            worldType: worldType.currentValue,
            layers: worldType.currentValue === "flat" ? flatLayers.text : "",
            gameMode: gameMode.currentValue,
            worldOptions: page.game.worldOptions.values
        });
    }

    // What the world generation options say in a line: their preset, or how many differ.
    function optionsSummary(): string {
        const options = page.game.worldOptions;
        if (options.changedCount === 0)
            return qsTr("Every option at its default.");
        const preset = options.presets.find(each => each.name === options.preset);
        return preset ? qsTr("Preset: %1").arg(preset.title) : qsTr("%n option(s) changed.", "", options.changedCount);
    }

    // Back from the options: the button that opened them has the focus again.
    function focusOptions() {
        moreOptions.forceActiveFocus(Qt.TabFocusReason);
    }

    spacing: 0

    Binding {
        target: page.game.worldOptions.preview
        property: "seedText"
        value: page.worldSeed()
    }

    SettingsPage {
        id: form

        objectName: "newWorldForm"
        Layout.fillWidth: true
        Layout.fillHeight: true
        title: qsTr("New world")

        SettingGroup {
            title: qsTr("World")

            SettingRow {
                label: qsTr("Name")

                GameTextField {
                    id: worldName

                    Layout.fillWidth: true
                    Accessible.name: qsTr("World name")
                    text: qsTr("New World")
                    onAccepted: page.create()
                }
            }
            SettingDropdown {
                id: worldType

                objectName: "worldTypeChoice"
                label: qsTr("World type")
                help: worldType.currentValue === "flat" ? qsTr("The same layers everywhere, no caves or trees, no creatures of their own accord.")
                      : worldType.currentValue === "void" ? qsTr("Nothing but a small platform; noon, no creatures of their own accord.")
                      : worldType.currentValue === "showcase" ? qsTr("A flat world with every block, creature and item on show, at noon.")
                      : qsTr("Hills, mountains, caves, oceans and biomes.")
                textRole: "text"
                valueRole: "value"
                model: [
                    { value: "terrain", text: qsTr("Normal") },
                    { value: "flat", text: qsTr("Flat") },
                    { value: "void", text: qsTr("Void") },
                    { value: "showcase", text: qsTr("Showcase") }
                ]
            }
            SettingRow {
                visible: page.terrain
                label: qsTr("Seed")
                help: qsTr("A number or a text; empty for a random one.")

                GameTextField {
                    id: seed

                    objectName: "worldSeed"
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Seed")
                    onAccepted: page.create()
                }
            }
            SettingRow {
                visible: worldType.currentValue === "flat"
                label: qsTr("Layers")
                help: qsTr("From the bottom up; empty for the default.")

                GameTextField {
                    id: flatLayers

                    Layout.fillWidth: true
                    Accessible.name: qsTr("Layers from the bottom up")
                    placeholderText: visible ? page.game.defaultFlatLayers() : ""
                    onAccepted: page.create()
                }
            }
        }

        SettingGroup {
            visible: page.terrain
            title: qsTr("World generation")

            SettingRow {
                label: qsTr("Villages, biomes, caves …")
                help: page.optionsSummary()

                GameButton {
                    id: moreOptions

                    objectName: "moreWorldOptions"
                    text: qsTr("More world options …")
                    onClicked: page.optionsRequested()
                }
                Item {
                    Layout.fillWidth: true
                }
            }
        }

        MapPreviewGroup {
            visible: page.terrain
            flickable: form
            preview: page.game.worldOptions.preview
            randomSeed: seed.text.trim().length === 0
            onAnotherSeedRequested: page.drawSeed()
        }

        SettingGroup {
            title: qsTr("Rules")

            SettingDropdown {
                id: gameMode

                objectName: "gameModeChoice"
                label: qsTr("Game mode")
                help: gameMode.currentValue === "creative"
                      ? qsTr("Every block and item without limit, flying, instant breaking, no damage or hunger.")
                      : gameMode.currentValue === "adventure"
                      ? qsTr("Survival for prepared maps: blocks break and items are placed only where /adventure allows it.")
                      : gameMode.currentValue === "spectator"
                      ? qsTr("Fly through blocks and look around, unseen; nothing can be touched.")
                      : qsTr("Gather, craft and survive: health, hunger and creatures count.")
                textRole: "text"
                valueRole: "value"
                model: [
                    { value: "survival", text: qsTr("Survival") },
                    { value: "creative", text: qsTr("Creative") },
                    { value: "adventure", text: qsTr("Adventure") },
                    { value: "spectator", text: qsTr("Spectator") }
                ]
            }
            SettingCheck {
                id: keepInventory

                Layout.minimumHeight: Ui.size.row
                checked: true
                text: qsTr("Keep inventory on death")
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
            objectName: "createWorldButton"
            kind: GameButton.Primary
            large: true
            text: qsTr("Create and play")
            onClicked: page.create()
        }
    }
}
